/* =============================================================================
 * drivers/svga/svga.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Sceglie la risoluzione della console.
 *
 *     /dev/svga.drv                dice com'e' configurata
 *     /dev/svga.drv 800x600        la cambia (serve un riavvio)
 *     /dev/svga.drv testo          torna alla console di testo 80x25
 *     /dev/svga.drv -n <modo>      dice cosa farebbe, senza scrivere
 *     /dev/svga.drv -i             la sonda dei driver (vedi hwconfig -d)
 *
 * -----------------------------------------------------------------------------
 * ! STA FRA I DRIVER, E NON E' SOLO UN NOME
 *
 * Non registra un servizio IPC e non guida una periferica: quello lo fa
 * Stage 2, e lo fa una volta sola prima del modo protetto. Sta in /dev con
 * l'estensione .drv perche' cosi' entra nel CATALOGO dei driver, viene
 * sondato con `-i` come tutti gli altri, e `hwconfig -d` lo installa da
 * solo sul disco insieme a kbd.drv. In /bin sarebbe stato un file da
 * ricordarsi di copiare a mano su ogni sistema installato.
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' SI SCRIVE IN DUE POSTI, E PERCHE' NON SONO DUE VERITA'
 *
 * Una modalita' grafica si imposta con INT 10h, cioe' con il BIOS, cioe' in
 * MODO REALE. Quando il kernel comincia a girare quella porta e' gia'
 * chiusa: l'unico che puo' farlo e' Stage 2, prima del passaggio a modo
 * protetto. E Stage 2 non ha un filesystem — riceve da Stage 1 una mappa di
 * settori gia' pronta per il kernel, e da disco rigido nemmeno quella —
 * quindi /boot/kernel.cfg non puo' andarselo a leggere.
 *
 * Percio':
 *
 *   /boot/kernel.cfg       la CONFIGURAZIONE. E' quella che si legge, che
 *                          `hwconfig` mostra, e che sopravvive a un
 *                          aggiornamento del bootloader.
 *
 *   un byte dentro         il RECAPITO. Marcato dalla firma 'SVGAMODE',
 *   LOADER.BIN/stage2      dentro il binario di Stage 2, che lo legge da se'
 *                          stesso senza toccare il filesystem.
 *
 * Il secondo non e' una seconda configurazione: e' una copia che solo questo
 * programma scrive, e che il kernel CONFRONTA a ogni avvio con la voce di
 * kernel.cfg. Se divergono lo dice, invece di lasciare che una macchina si
 * avvii in una risoluzione che nessun file dichiara.
 *
 * E' lo stesso patto della mappa di settori che `install` scrive nel settore
 * di avvio: il programma che gira DENTRO EX-OS, e che il filesystem ce l'ha,
 * prepara cio' che serve a chi si avvia e il filesystem non ce l'ha ancora.
 *
 * ! LA FIRMA SI CERCA, NON SI CALCOLA. Un offset fisso dentro Stage 2
 * cambierebbe a ogni riga aggiunta a loader.asm, e questo programma
 * finirebbe a scrivere in mezzo al codice — di un file che serve ad avviare
 * la macchina.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `svga.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("svga.drv", "0.001");

#define PERC_MAX  256
#define S2_MAX    65536
#define CFG_MAX   16384
#define RIGA_MAX  256

static const char FIRMA[8] = { 'S','V','G','A','M','O','D','E' };

/* I modi, nell'ordine in cui Stage 2 li ha in tabella: l'indice E' il byte
 * che si scrive. Cambiare qui senza cambiare svgatab in loader.asm
 * darebbe una risoluzione diversa da quella chiesta. */
static const char *nome_modo[4] = { "testo", "640x480", "800x600", "1024x768" };
static const char *dice_modo[4] = {
    "console di testo 80x25 (la scheda ci mette i glifi)",
    "80x30 caratteri",
    "100x37 caratteri",
    "128x48 caratteri"
};

/* Dove puo' stare l'immagine di Stage 2. Non e' un elenco di configurazioni:
 * e' un elenco di posti dove guardare, come un PATH.
 *
 *   /LOADER.BIN   avviando da floppy o da CD: e' il nome che Stage 1 cerca
 *                 nella root del volume.
 *   /boot/stage2.bin  su un disco installato. ! IL NOME LO DECIDE
 *                 install, non noi: sostituisce("stage2") produce
 *                 "stage2.bin", e cercare "stage2" faceva rispondere
 *                 «non trovo l'immagine di Stage 2» proprio sul disco
 *                 installato, cioe' nell'unico posto dove questo comando
 *                 serve davvero — dal CD non si puo' scrivere comunque. */
static const char *posti[] = { "/LOADER.BIN", "/boot/stage2.bin", NULL };

static char       g_s2[PERC_MAX] = "";
static char       g_cfg[PERC_MAX] = "/boot/kernel.cfg";
static int        opt_n = 0;

static unsigned char immagine[S2_MAX];
static long        dim_imm = 0;
static long        pos_firma = -1;


/* ─────────────────────────────────────────────────────────────────────────────
 * L'immagine di Stage 2
 * ───────────────────────────────────────────────────────────────────────────── */

/* Carica l'immagine e trova la firma. Rende 0 se ci e' riuscito. */
static int carica_stage2(void)
{
    int  i, fd, n;
    long k;

    if (g_s2[0] == '\0') {
        for (i = 0; posti[i]; i++) {
            if (access(posti[i], F_OK) == 0) {
                strncpy(g_s2, posti[i], PERC_MAX - 1);
                g_s2[PERC_MAX - 1] = '\0';
                break;
            }
        }
    }
    if (g_s2[0] == '\0') {
        printf("svga: non trovo l'immagine di Stage 2. Cercata in:\n");
        for (i = 0; posti[i]; i++) printf("        %s\n", posti[i]);
        return -1;
    }

    fd = open(g_s2, O_RDONLY);
    if (fd < 0) { printf("svga: %s: %s\n", g_s2, strerror(errno)); return -1; }

    dim_imm = 0;
    while ((n = (int)read(fd, immagine + dim_imm,
                          (unsigned int)(S2_MAX - dim_imm))) > 0) {
        dim_imm += n;
        if (dim_imm >= S2_MAX) break;
    }
    close(fd);
    if (dim_imm <= 0) { printf("svga: %s e' vuoto\n", g_s2); return -1; }

    pos_firma = -1;
    for (k = 0; k + 8 <= dim_imm; k++) {
        if (memcmp(immagine + k, FIRMA, 8) == 0) { pos_firma = k; break; }
    }
    if (pos_firma < 0 || pos_firma + 8 >= dim_imm) {
        printf("svga: in %s non c'e' la firma 'SVGAMODE'.\n", g_s2);
        printf("      E' uno Stage 2 anteriore alla modalita' grafica: si\n");
        printf("      aggiorna con `install`, oppure riscrivendo il floppy.\n");
        return -1;
    }
    return 0;
}

/* Riscrive l'immagine con il byte cambiato. Stessa lunghezza, stesso file:
 * e' cio' che tiene valida la mappa di settori del disco, che punta a
 * settori precisi e non sopravviverebbe a un file spostato. */
static int salva_stage2(void)
{
    int  fd, scritti = 0, w;

    fd = open(g_s2, O_WRONLY);
    if (fd < 0) { printf("svga: %s: %s\n", g_s2, strerror(errno)); return -1; }

    while (scritti < dim_imm) {
        w = (int)write(fd, immagine + scritti,
                       (unsigned int)(dim_imm - scritti));
        if (w <= 0) {
            printf("svga: %s: scrittura interrotta a %d byte\n", g_s2, scritti);
            close(fd);
            return -1;
        }
        scritti += w;
    }
    close(fd);
    return 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * La voce in kernel.cfg
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * ! SI MODIFICA LA RIGA, NON SI RIGENERA IL FILE. Il kernel.cfg che viene
 * con il sistema e' duecento righe di spiegazioni, montaggi e disposizione
 * di tastiera: riscriverlo per cambiare una parola vorrebbe dire buttare via
 * tutto il resto. `hwconfig` lo rigenera per intero, ma li' sta proponendo
 * una configurazione completa e lo dice prima di farlo.
 * ───────────────────────────────────────────────────────────────────────────── */

static int riga_e_svga(const char *r)
{
    int i = 0;

    while (r[i] == ' ' || r[i] == '\t') i++;
    if (r[i] != 's' && r[i] != 'S') return 0;
    if (strncmp(r + i, "svga", 4) != 0 && strncmp(r + i, "SVGA", 4) != 0)
        return 0;
    i += 4;
    while (r[i] == ' ' || r[i] == '\t') i++;
    return (r[i] == '=');
}

/* Legge il valore della voce svga da kernel.cfg. Rende -1 se non c'e'. */
static int leggi_cfg(char *dst, int max)
{
    static char testo[CFG_MAX];
    int  fd, n, i = 0, in_kernel = 0;

    fd = open(g_cfg, O_RDONLY);
    if (fd < 0) return -1;
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';

    while (i < n) {
        char riga[RIGA_MAX];
        int  l = 0, k = 0;

        while (i < n && testo[i] != '\n' && l < RIGA_MAX - 1) riga[l++] = testo[i++];
        riga[l] = '\0';
        if (i < n && testo[i] == '\n') i++;

        while (riga[k] == ' ' || riga[k] == '\t') k++;
        if (riga[k] == '[')
            in_kernel = (strncmp(riga + k, "[kernel]", 8) == 0);
        if (riga[k] == '#') continue;

        if (in_kernel && riga_e_svga(riga)) {
            const char *v = riga;
            int j = 0;

            while (*v && *v != '=') v++;
            if (*v == '=') v++;
            while (*v == ' ' || *v == '\t') v++;
            while (v[j] && v[j] != ' ' && v[j] != '\t' && v[j] != '#' &&
                   j < max - 1) { dst[j] = v[j]; j++; }
            dst[j] = '\0';
            return (j > 0) ? 0 : -1;
        }
    }
    return -1;
}

static int scrivi_cfg(const char *valore)
{
    static char testo[CFG_MAX];
    static char nuovo[CFG_MAX];
    char bak[PERC_MAX];
    int  fd, n, i = 0, o = 0, in_kernel = 0, fatto = 0, visto_kernel = 0;

    /* ! SI SOSTITUISCE L'ESTENSIONE, NON SE NE AGGIUNGE UNA.
     * "kernel.cfg.bak" ha due punti e su FAT non e' un nome 8.3 valido: la
     * scrittura dei nomi lunghi non c'e' ancora, e su un disco installato
     * il salvataggio falliva con «non e' un nome 8.3 valido» — un errore
     * che parla di nomi mentre l'utente stava cambiando la risoluzione.
     * "kernel.bak" ci sta, e dice la stessa cosa. */
    {
        int i = 0, punto = -1;

        while (g_cfg[i] && i < PERC_MAX - 5) {
            if (g_cfg[i] == '.') punto = i;
            if (g_cfg[i] == '/') punto = -1;   /* punti nelle directory: no */
            bak[i] = g_cfg[i];
            i++;
        }
        if (punto >= 0) i = punto;
        bak[i++] = '.'; bak[i++] = 'b'; bak[i++] = 'a'; bak[i++] = 'k';
        bak[i] = '\0';
    }

    fd = open(g_cfg, O_RDONLY);
    if (fd < 0) {
        printf("svga: %s: %s\n", g_cfg, strerror(errno));
        printf("      La configurazione non e' stata toccata. Aggiungi a\n");
        printf("      mano, nella sezione [kernel]:  svga = %s\n", valore);
        return -1;
    }
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';

    while (i < n) {
        char riga[RIGA_MAX];
        int  l = 0, k = 0;

        while (i < n && testo[i] != '\n' && l < RIGA_MAX - 1) riga[l++] = testo[i++];
        riga[l] = '\0';
        if (i < n && testo[i] == '\n') i++;

        while (riga[k] == ' ' || riga[k] == '\t') k++;
        if (riga[k] == '[') {
            in_kernel = (strncmp(riga + k, "[kernel]", 8) == 0);
            if (in_kernel) visto_kernel = 1;
        }

        if (in_kernel && !fatto && riga[k] != '#' && riga_e_svga(riga)) {
            o += snprintf(nuovo + o, CFG_MAX - o, "svga      = %s\n", valore);
            fatto = 1;
            continue;
        }

        o += snprintf(nuovo + o, CFG_MAX - o, "%s\n", riga);
        if (o >= CFG_MAX - RIGA_MAX) {
            printf("svga: %s e' piu' grande di %d byte: non lo riscrivo\n",
                   g_cfg, CFG_MAX);
            return -1;
        }

        /* Voce assente: la si mette subito sotto l'intestazione [kernel],
         * dove chi legge il file se l'aspetta, invece che in fondo. */
        if (in_kernel && !fatto && riga[k] == '[') {
            o += snprintf(nuovo + o, CFG_MAX - o,
                          "# Risoluzione della console. La imposta Stage 2\n"
                          "# prima del modo protetto; la cambia `svga`.\n"
                          "svga      = %s\n", valore);
            fatto = 1;
        }
    }

    if (!fatto) {
        o += snprintf(nuovo + o, CFG_MAX - o,
                      "\n%s"
                      "# Aggiunta da svga.\n"
                      "svga      = %s\n",
                      visto_kernel ? "" : "[kernel]\n", valore);
    }

    /* Il file di prima non si perde: stessa regola di hwconfig. */
    {
        int fo = open(bak, O_WRONLY | O_CREAT | O_TRUNC);

        if (fo >= 0) { write(fo, testo, (unsigned int)n); close(fo); }
    }

    fd = open(g_cfg, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("svga: %s: %s\n", g_cfg, strerror(errno)); return -1; }
    {
        int scritti = 0, w;

        while (scritti < o) {
            w = (int)write(fd, nuovo + scritti, (unsigned int)(o - scritti));
            if (w <= 0) { close(fd); printf("svga: scrittura interrotta\n"); return -1; }
            scritti += w;
        }
    }
    close(fd);
    return 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Comando
 * ───────────────────────────────────────────────────────────────────────────── */

static int modo_da_nome(const char *s)
{
    int i;

    for (i = 0; i < 4; i++) if (strcmp(s, nome_modo[i]) == 0) return i;
    /* Sinonimi comodi: chi scrive "80x25" intende il modo testo. */
    if (strcmp(s, "80x25") == 0 || strcmp(s, "text") == 0) return 0;
    return -1;
}

static void mostra(void)
{
    char in_cfg[32];
    int  bl = immagine[pos_firma + 8];
    int  ok_cfg;

    printf("Risoluzione della console\n\n");

    if (bl < 0 || bl > 3) {
        printf("  bootloader   valore %d non valido in %s\n", bl, g_s2);
        printf("               (si rimette con `svga testo`)\n");
    } else {
        printf("  bootloader   %-9s  %s\n", nome_modo[bl], dice_modo[bl]);
        printf("               in %s\n", g_s2);
    }

    ok_cfg = (leggi_cfg(in_cfg, sizeof(in_cfg)) == 0);
    if (ok_cfg) printf("  kernel.cfg   %s\n", in_cfg);
    else        printf("  kernel.cfg   voce assente\n");

    /* ! LA DIVERGENZA SI DICE, non si sistema di nascosto. Sistemarla
     * vorrebbe dire scegliere quale delle due ha ragione, e non si puo'
     * saperlo: una macchina il cui bootloader e' stato aggiornato e una a
     * cui qualcuno ha modificato kernel.cfg a mano si presentano identiche. */
    if (ok_cfg && bl >= 0 && bl <= 3 && strcmp(in_cfg, nome_modo[bl]) != 0) {
        printf("\n  !  I due non dicono la stessa cosa. Comanda il\n");
        printf("      bootloader: il kernel non puo' impostare una\n");
        printf("      modalita' grafica, quella porta si chiude prima che\n");
        printf("      cominci a girare.\n");
        printf("      Si allineano con:  /dev/svga.drv %s\n", in_cfg);
    }

    printf("\n  disponibili  testo  640x480  800x600  1024x768\n");
    printf("\nSi cambia con `svga <modo>`, e vale dal prossimo avvio.\n");
    printf("La modalita' ATTIVA ADESSO la stampa il kernel all'accensione\n");
    printf("(riga \"Video\" del log di avvio).\n");
}

static void uso(void)
{
    printf("uso: /dev/svga.drv [-n] [modo]\n\n");
    printf("Sceglie la risoluzione della console. Senza argomenti dice\n");
    printf("com'e' configurata adesso.\n\n");
    printf("  testo      console di testo 80x25\n");
    printf("  640x480    80x30 caratteri\n");
    printf("  800x600    100x37 caratteri\n");
    printf("  1024x768   128x48 caratteri\n\n");
    printf("  -n         dice cosa farebbe, senza scrivere niente\n\n");
    printf("Scrive la voce in /boot/kernel.cfg e ritocca il byte marcato\n");
    printf("'SVGAMODE' dentro l'immagine di Stage 2: e' da li' che la\n");
    printf("risoluzione viene impostata, prima del modo protetto, perche'\n");
    printf("dopo il BIOS non e' piu' raggiungibile.\n\n");
    printf("SERVE UN RIAVVIO perche' abbia effetto. Se la scheda non offre\n");
    printf("la risoluzione chiesta, Stage 2 ripiega sul testo invece di\n");
    printf("lasciare uno schermo nero.\n");
}

int main(int argc, char **argv)
{
    const char *voluto = NULL;
    int i, modo;

    for (i = 1; i < argc; i++) {
        /* La convenzione comune a tutti i driver: sonda, di' cosa hai
         * trovato, esci con 0 se servi su questa macchina.
         *
         * ! QUI LA RISPOSTA E' SEMPRE SI', E VA DETTO PERCHE'. Non c'e'
         * hardware da sondare: la risoluzione la si sceglie, non la si
         * trova. Ma un sistema installato senza questo programma e' un
         * sistema in cui la console non si puo' piu' cambiare — e non si
         * puo' nemmeno rimediare, perche' il rimedio e' questo file. */
        if (strcmp(argv[i], "-i") == 0) {
            printf("svga: sceglie la risoluzione della console.\n");
            printf("      Nessuna periferica propria: la modalita' la\n");
            printf("      imposta Stage 2. Serve su qualunque macchina.\n");
            return 0;
        }
        if      (strcmp(argv[i], "-n") == 0) opt_n = 1;
        else if (strcmp(argv[i], "-h") == 0 ||
                 strcmp(argv[i], "-help") == 0 ||
                 strcmp(argv[i], "--help") == 0) { uso(); return 0; }
        else if (argv[i][0] == '-') { uso(); return 1; }
        else voluto = argv[i];
    }

    if (carica_stage2() != 0) return 1;

    if (voluto == NULL) { mostra(); return 0; }

    modo = modo_da_nome(voluto);
    if (modo < 0) {
        printf("svga: '%s' non e' una risoluzione che conosco.\n", voluto);
        printf("      Sono: testo, 640x480, 800x600, 1024x768.\n");
        return 1;
    }

    printf("Risoluzione: %s (%s)\n\n", nome_modo[modo], dice_modo[modo]);
    printf("  %s        voce svga\n", g_cfg);
    printf("  %s        byte alla firma 'SVGAMODE' (offset %ld)\n",
           g_s2, pos_firma + 8);

    if (opt_n) { printf("\n(-n: non ho scritto niente)\n"); return 0; }

    immagine[pos_firma + 8] = (unsigned char)modo;
    if (salva_stage2() != 0) return 1;
    printf("\n  scritto %s\n", g_s2);

    if (scrivi_cfg(nome_modo[modo]) != 0) {
        printf("\n!  Il bootloader e' stato aggiornato ma kernel.cfg no.\n");
        printf("   Il prossimo avvio userebbe %s con la configurazione che\n",
               nome_modo[modo]);
        printf("   dice altro. Rimetti a posto la voce a mano.\n");
        return 1;
    }
    printf("  scritto %s  (il precedente e' in %s.bak)\n", g_cfg, g_cfg);

    /* ! IL RIAVVIO SI DICE FORTE. Chi ha appena scritto `svga 800x600` si
     * aspetta di vedere lo schermo cambiare, e non cambia niente: la
     * modalita' la imposta Stage 2, che ha finito il proprio lavoro
     * all'accensione. Senza questa riga si conclude che il comando non ha
     * funzionato, e lo si rilancia. */
    printf("\n  ====================================================\n");
    printf("   SERVE UN RIAVVIO perche' abbia effetto.\n");
    printf("   La console resta %s fino ad allora: la\n",
           "quella di adesso");
    printf("   risoluzione la imposta Stage 2 all'accensione, e da\n");
    printf("   sistema acceso non e' piu' raggiungibile.\n");
    printf("  ====================================================\n");
    printf("\n   reboot     riavvia adesso\n");
    return 0;
}

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
EX_VERSIONE("svga.drv", "0.003");

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

/* ! -16 NON CAMBIA LA RISOLUZIONE, CAMBIA QUANTI BYTE SI SPOSTANO. Accende il
 * bit 7 del byte di modo, che Stage 2 legge come «preferisci 16 bpp invece di
 * 32». A 800x600 lo scorrimento della console passa da 1,8 MB a 900 KB per
 * riga: su una macchina del 2004, con la memoria video sul bus e non in cache,
 * si sente.
 *
 * ! E NON E' GRATIS: il 5-6-5 butta i bit bassi di ogni componente, quindi le
 * sfumature si impastano. Su una macchina veloce non conviene, e infatti il
 * predefinito non cambia: si chiede. */
static int        opt_16 = 0;

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
                          "# prima del modo protetto; la cambia `/dev/svga.drv`.\n"
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

/* ─────────────────────────────────────────────────────────────────────────────
 * Any resolution the card offers (17 October 2026, @RISOLUZIONI)
 *
 * ! MODE 4 IS "THE ONE IN svgacust": Stage 2 keeps, right after its table of
 * three, two words that this program writes - width and height - and mode 4
 * means "look for that one" (bootloader/stage2/loader.asm). They sit at the
 * signature + 21; a Stage 2 older than that has 'RAMDISCO' there, and then
 * only the three of the table are possible.
 * ───────────────────────────────────────────────────────────────────────────── */
#define OFF_MODO   8
#define OFF_CUST   21
#define PROVA_F    "/boot/svgaprov.cfg"   /* a mode on trial: what to put back */
#define VISTA_F    "/boot/svgavist.cfg"   /* the desktop has shown the question */

static char g_nome_cust[24];

static int ha_cust(void)
{
    return pos_firma + OFF_CUST + 4 + 2 + 8 <= dim_imm &&
           memcmp(immagine + pos_firma + OFF_CUST + 6, "RAMDISCO", 8) == 0;
}

static unsigned int cust_w(void)
{
    return immagine[pos_firma + OFF_CUST] | (unsigned)immagine[pos_firma + OFF_CUST + 1] << 8;
}

static unsigned int cust_h(void)
{
    return immagine[pos_firma + OFF_CUST + 2] | (unsigned)immagine[pos_firma + OFF_CUST + 3] << 8;
}

static const char *nome_di(int modo)
{
    if (modo >= 0 && modo <= 3) return nome_modo[modo];
    if (modo == 4) {
        snprintf(g_nome_cust, sizeof(g_nome_cust), "%ux%u", cust_w(), cust_h());
        return g_nome_cust;
    }
    return "?";
}

/* "WxH" -> 1 and the two numbers. */
static int leggi_wxh(const char *s, unsigned int *w, unsigned int *h)
{
    char *x;

    *w = (unsigned int)strtoul(s, &x, 10);
    if (*w == 0 || (*x != 'x' && *x != 'X')) return 0;
    *h = (unsigned int)strtoul(x + 1, &x, 10);
    return *h != 0 && *x == '\0';
}

/* The modes the card listed at boot, sorted, without repeats of the same
 * size and depth. How many; 0 when the system started in text mode. */
static VideoModo g_modi[64];

static int carica_modi(void)
{
    int n = video_modi(g_modi, 64), i, j;

    if (n < 0) n = 0;
    if (n > 64) n = 64;
    for (i = 1; i < n; i++) {
        VideoModo t = g_modi[i];

        j = i - 1;
        while (j >= 0 && (g_modi[j].larghezza > t.larghezza ||
               (g_modi[j].larghezza == t.larghezza && (g_modi[j].altezza > t.altezza ||
               (g_modi[j].altezza == t.altezza && g_modi[j].bit > t.bit))))) {
            g_modi[j + 1] = g_modi[j];
            j--;
        }
        g_modi[j + 1] = t;
    }
    for (i = j = 0; i < n; i++) {
        if (j > 0 && g_modi[j - 1].larghezza == g_modi[i].larghezza &&
            g_modi[j - 1].altezza == g_modi[i].altezza && g_modi[j - 1].bit == g_modi[i].bit)
            continue;
        g_modi[j++] = g_modi[i];
    }
    return j;
}

/* Does the card offer w x h (at 16 bpp if `sedici`, at any depth otherwise)?
 * 1 yes, 0 no, -1 the list is not known. */
static int offerto(unsigned int w, unsigned int h, int sedici)
{
    int n = carica_modi(), i;

    if (n == 0) return -1;
    for (i = 0; i < n; i++)
        if (g_modi[i].larghezza == w && g_modi[i].altezza == h &&
            (!sedici || g_modi[i].bit == 16)) return 1;
    return 0;
}

/* Which card and which screen (kernel 0.267, @SCHEDA-VIDEO): what Stage 2
 * read from the video BIOS and from the screen's EDID. Nothing when Stage 2
 * did not look (text mode, older kernel or Stage 2). */
static void scheda_e_schermo(int n)
{
    VideoScheda  sc;
    VideoSchermo sm;
    int          i, nativo = 0;

    if (video_scheda(&sc) != 1) return;
    printf("Scheda:   %s%s%s", sc.fornitore[0] ? sc.fornitore : sc.oem,
           sc.prodotto[0] ? " - " : "", sc.prodotto);
    if (sc.revisione[0]) printf(" (%s)", sc.revisione);
    printf("\n          VBE %u.%u, %u MB di memoria video\n", sc.vbe_versione >> 8,
           sc.vbe_versione & 0xFF, (sc.memoria_64k + 15u) / 16u);
    if (!video_schermo(&sc, &sm)) {
        printf("Schermo:  il BIOS non sa leggerne i dati (EDID)\n\n");
        return;
    }
    printf("Schermo:  %s%s%s, %u x %u cm\n", sm.nome[0] ? sm.nome : "",
           sm.nome[0] ? " " : "", sm.fabbricante, sm.cm_largo, sm.cm_alto);
    if (sm.larghezza == 0) { printf("\n"); return; }
    for (i = 0; i < n; i++)
        if (g_modi[i].larghezza == sm.larghezza && g_modi[i].altezza == sm.altezza) nativo = 1;
    printf("          risoluzione nativa %ux%u%s\n\n", sm.larghezza, sm.altezza,
           nativo ? "" : " - NON fra i modi del BIOS: il pannello\n"
                         "          ingrandisce quelli che ci sono");
}

static void lista(void)
{
    VideoInfo v;
    int       n = carica_modi(), i;

    memset(&v, 0, sizeof(v));
    video_info(&v);
    if (n == 0) {
        printf("svga: la lista dei modi non c'e'. Stage 2 la legge solo quando gli\n");
        printf("      si chiede la grafica: con la console di testo non l'ha guardata.\n");
        printf("      Prima `/dev/svga.drv 800x600`, un riavvio, poi di nuovo -lista.\n");
        return;
    }
    scheda_e_schermo(n);
    printf("I modi che la scheda offre (VBE, framebuffer lineare):\n\n");
    for (i = 0; i < n; i++) {
        int adesso = v.larghezza == g_modi[i].larghezza && v.altezza == g_modi[i].altezza &&
                     v.bit == g_modi[i].bit;

        printf("  %4ux%-4u  %2u bit  %s%s\n", g_modi[i].larghezza, g_modi[i].altezza,
               g_modi[i].bit, g_modi[i].bit == 16 ? "(65 mila colori)  " : "(16 milioni)      ",
               adesso ? "<- adesso" : "");
    }
    printf("\nSi sceglie con `/dev/svga.drv LARGHEZZAxALTEZZA` (con -16 per i 16 bit),\n");
    printf("e vale dal prossimo avvio.\n");
}

static int modo_da_nome(const char *s)
{
    int i;

    for (i = 0; i < 4; i++) if (strcmp(s, nome_modo[i]) == 0) return i;
    /* Sinonimi comodi: chi scrive "80x25" intende il modo testo. */
    if (strcmp(s, "80x25") == 0 || strcmp(s, "text") == 0) return 0;
    {
        unsigned int w, h;

        if (leggi_wxh(s, &w, &h)) return 4;
    }
    return -1;
}

static void mostra(void)
{
    char in_cfg[32];
    int  grezzo = immagine[pos_firma + OFF_MODO];
    int  bl = grezzo & 0x7F;
    int  ok_cfg;

    printf("Risoluzione della console\n\n");

    /* ! THE BIT 7 (-16) IS MASKED before looking at the mode: until 0.002 a
     * console set with -16 was reported as "valore non valido". */
    if (bl < 0 || bl > 4 || (bl == 4 && !ha_cust())) {
        printf("  bootloader   valore %d non valido in %s\n", grezzo, g_s2);
        printf("               (si rimette con `/dev/svga.drv testo`)\n");
    } else {
        printf("  bootloader   %-9s  %s%s\n", nome_di(bl),
               bl <= 3 ? dice_modo[bl] : "scelta dalla lista della scheda",
               (grezzo & 0x80) ? ", 16 bit" : "");
        printf("               in %s\n", g_s2);
    }

    ok_cfg = (leggi_cfg(in_cfg, sizeof(in_cfg)) == 0);
    if (ok_cfg) printf("  kernel.cfg   %s\n", in_cfg);
    else        printf("  kernel.cfg   voce assente\n");

    /* ! LA DIVERGENZA SI DICE, non si sistema di nascosto. Sistemarla
     * vorrebbe dire scegliere quale delle due ha ragione, e non si puo'
     * saperlo: una macchina il cui bootloader e' stato aggiornato e una a
     * cui qualcuno ha modificato kernel.cfg a mano si presentano identiche. */
    if (ok_cfg && bl >= 0 && bl <= 4 && strcmp(in_cfg, nome_di(bl)) != 0) {
        printf("\n  !  I due non dicono la stessa cosa. Comanda il\n");
        printf("      bootloader: il kernel non puo' impostare una\n");
        printf("      modalita' grafica, quella porta si chiude prima che\n");
        printf("      cominci a girare.\n");
        printf("      Si allineano con:  /dev/svga.drv %s\n", in_cfg);
    }
    if (access(PROVA_F, F_OK) == 0)
        printf("\n  !  Un modo e' IN PROVA: senza conferma torna quello di prima\n"
               "     (`/dev/svga.drv -conferma` lo tiene).\n");

    printf("\n  disponibili  testo  640x480  800x600  1024x768%s\n",
           ha_cust() ? ", e ogni modo di `/dev/svga.drv -lista`" : "");
    /* ! IL NOME PER INTERO, E NON «svga». Questo programma sta in /dev con
     * l'estensione .drv — cosi' entra nel catalogo dei driver — quindi in
     * PATH non c'e' nessun `svga` da digitare. */
    printf("\nSi cambia con `/dev/svga.drv <modo>`, e vale dal prossimo avvio.\n");
    printf("La modalita' ATTIVA ADESSO la stampa il kernel all'accensione\n");
    printf("(riga \"Video\" del log di avvio).\n");
}

static void uso(void)
{
    printf("uso: /dev/svga.drv [-n] [-16] [modo]\n");
    printf("     /dev/svga.drv -lista\n");
    printf("     /dev/svga.drv -prova modo [-16] | -conferma | -annulla | -guardia [s]\n\n");
    printf("Sceglie la risoluzione della console. Senza argomenti dice\n");
    printf("com'e' configurata adesso.\n\n");
    printf("  testo      console di testo 80x25\n");
    printf("  640x480    80x30 caratteri\n");
    printf("  800x600    100x37 caratteri\n");
    printf("  1024x768   128x48 caratteri\n");
    printf("  LxA        un altro modo della scheda: `-lista` li dice\n\n");
    printf("  -lista     i modi che la scheda offre (letti all'avvio)\n");
    printf("  -n         dice cosa farebbe, senza scrivere niente\n");
    printf("  -16        16 bit per pixel invece di 32: meta' dei byte\n");
    printf("             da spostare a ogni scorrimento, e una console\n");
    printf("             molto piu' svelta su una macchina lenta. Si\n");
    printf("             perdono le sfumature (5-6-5).\n\n");
    printf("  -prova     come sopra, ma IN PROVA: al riavvio la scrivania chiede\n");
    printf("             conferma per 30 secondi; senza risposta (anche con lo\n");
    printf("             schermo nero) torna il modo di prima e riavvia da sola.\n");
    printf("  -conferma  tiene il modo in prova     -annulla  rimette quello di prima\n");
    printf("  -guardia   all'avvio (avvio.sh): se la prova non viene confermata\n");
    printf("             entro s secondi (120), rimette il modo di prima e riavvia\n\n");
    printf("Scrive la voce in /boot/kernel.cfg e ritocca il byte marcato\n");
    printf("'SVGAMODE' dentro l'immagine di Stage 2: e' da li' che la\n");
    printf("risoluzione viene impostata, prima del modo protetto, perche'\n");
    printf("dopo il BIOS non e' piu' raggiungibile.\n\n");
    printf("SERVE UN RIAVVIO perche' abbia effetto. Se la scheda non offre\n");
    printf("la risoluzione chiesta, Stage 2 ripiega sul testo invece di\n");
    printf("lasciare uno schermo nero.\n");
}

/* La radice sta in RAM? Rende 1 se si', 0 se no o se non si sa.
 *
 * ! SERVE PERCHE' QUI SI SCRIVE SU UN FILE, E QUEL FILE PUO' EVAPORARE.
 * Quando Stage 2 copia il volume in memoria (vedi rd_addr in
 * kernel/include/kernel.h), la radice e' un disco che sparisce con la
 * corrente: questo programma scrive dentro /LOADER.BIN, dice «fatto», e al
 * riavvio non e' cambiato niente.
 *
 * ! E' SUCCESSO, il 10 settembre 2026: due referti della sonda presi in due
 * modalita' diverse sono usciti IDENTICI, tutti e due in modo testo. Il
 * comando aveva funzionato — su un disco che non esisteva piu'. Un lavoro che
 * riesce e non lascia traccia e' il modo peggiore di fallire, perche' non
 * assomiglia a un errore. */
static int radice_in_ram(void)
{
    MountInfo m[4];
    int n, i;

    n = mountinfo(m, 4, 0);
    if (n <= 0) return 0;

    for (i = 0; i < n; i++) {
        if (m[i].punto[0] != '/' || m[i].punto[1] != '\0') continue;
        return (strcmp(m[i].dev, "rd0") == 0);
    }
    return 0;
}

/* Writes the mode into the image and kernel.cfg. 0, or 1 with the reason
 * already printed. */
static int scrivi_modo(int modo, unsigned int w, unsigned int h, int sedici, const char *nome)
{
    /* ! IL BIT 7 SI SOMMA AL MODO, NON LO SOSTITUISCE: i bit bassi restano la
     * risoluzione, e Stage 2 se lo toglie di mezzo prima di guardare la
     * tabella. Vedi `want16` in bootloader/stage2/loader.asm. */
    immagine[pos_firma + OFF_MODO] = (unsigned char)(modo | (sedici ? 0x80 : 0));
    if (modo == 4) {
        immagine[pos_firma + OFF_CUST]     = (unsigned char)(w & 0xFF);
        immagine[pos_firma + OFF_CUST + 1] = (unsigned char)(w >> 8);
        immagine[pos_firma + OFF_CUST + 2] = (unsigned char)(h & 0xFF);
        immagine[pos_firma + OFF_CUST + 3] = (unsigned char)(h >> 8);
    }
    if (salva_stage2() != 0) return 1;
    printf("  scritto %s\n", g_s2);

    if (scrivi_cfg(nome) != 0) {
        printf("\n!  Il bootloader e' stato aggiornato ma kernel.cfg no.\n");
        printf("   Il prossimo avvio userebbe %s con la configurazione che\n", nome);
        printf("   dice altro. Rimetti a posto la voce a mano.\n");
        return 1;
    }
    printf("  scritto %s  (il precedente e' in %s.bak)\n", g_cfg, g_cfg);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * THE TRIAL (@PANNELLO-VIDEO, 17 October 2026)
 *
 * ! A MODE THE MONITOR CANNOT SHOW MUST NOT NEED THE PERSON TO SEE ANYTHING.
 * The mode changes only through Stage 2 at boot, so "try it" is a reboot:
 * -prova keeps what to put back in /boot/svgaprov.cfg; at the next start the
 * desktop asks for 30 seconds (and writes /boot/svgavist.cfg as it does), and
 * -guardia, started by /boot/avvio.sh, puts the old mode back and reboots
 * when nobody confirmed - the desktop that never appeared included.
 * ───────────────────────────────────────────────────────────────────────────── */
/* ! open/write, NOT stdio: this driver links the libc statically and lives
 * on the 1.44 MB floppy, where buffered stdio cost 9 KB it did not have. */
static int prova_scrivi(const char *nuovo)
{
    char cfg[32] = "", t[320];
    int  fd, n;

    leggi_cfg(cfg, sizeof(cfg));
    n = snprintf(t, sizeof(t),
                 "# svga.drv -prova: il modo di prima, rimesso se la prova non viene confermata\n"
                 "byte  = %u\nlargo = %u\nalto  = %u\ncfg   = %s\nnuovo = %s\n",
                 immagine[pos_firma + OFF_MODO], ha_cust() ? cust_w() : 0,
                 ha_cust() ? cust_h() : 0, cfg[0] ? cfg : "testo", nuovo);
    fd = open(PROVA_F, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0 || write(fd, t, (unsigned int)n) != n) {
        printf("svga: non riesco a scrivere %s: %s\n", PROVA_F, strerror(errno));
        if (fd >= 0) close(fd);
        return 1;
    }
    close(fd);
    unlink(VISTA_F);
    return 0;
}

/* /boot/avvio.sh must start the guard: an installed system older than this
 * has no such line, and one is added (the file is root's, as this command). */
static void guardia_in_avvio(void)
{
    static char testo[16384];
    int         fd, n;

    fd = open("/boot/avvio.sh", O_RDONLY);
    if (fd < 0) return;
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (n < 0) n = 0;
    testo[n] = '\0';
    if (strstr(testo, "svga.drv -guardia")) return;
    fd = open("/boot/avvio.sh", O_WRONLY | O_APPEND);
    if (fd < 0) return;
    {
        const char *r = "\n# A video mode on trial goes back by itself if nobody confirms it.\n"
                        "/dev/svga.drv -guardia &\n";
        write(fd, r, (unsigned int)strlen(r));
    }
    close(fd);
    printf("  aggiunta a /boot/avvio.sh la riga `/dev/svga.drv -guardia &`\n");
}

static int annulla(void)
{
    char  testo[512], *riga, *fine, cfg[32] = "testo";
    unsigned int byte = 0, w = 0, h = 0;
    int   fd = open(PROVA_F, O_RDONLY), n;

    if (fd < 0) { printf("svga: nessun modo in prova.\n"); return 0; }
    n = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    testo[n > 0 ? n : 0] = '\0';
    for (riga = testo; *riga; riga = fine) {
        char *v;
        size_t l;

        fine = strchr(riga, '\n');
        if (fine) *fine++ = '\0'; else fine = riga + strlen(riga);
        v = strchr(riga, '=');
        if (!v || riga[0] == '#') continue;
        v++;
        while (*v == ' ') v++;
        l = strlen(v);
        while (l > 0 && (v[l - 1] == '\r' || v[l - 1] == ' ')) v[--l] = '\0';
        if (strncmp(riga, "byte", 4) == 0)  byte = (unsigned int)atoi(v);
        if (strncmp(riga, "largo", 5) == 0) w = (unsigned int)atoi(v);
        if (strncmp(riga, "alto", 4) == 0)  h = (unsigned int)atoi(v);
        if (strncmp(riga, "cfg", 3) == 0) { strncpy(cfg, v, sizeof(cfg) - 1); cfg[sizeof(cfg) - 1] = '\0'; }
    }
    printf("svga: rimetto il modo di prima (%s).\n", cfg);
    if (scrivi_modo((int)(byte & 0x7F), w, h, (byte & 0x80) != 0, cfg) != 0) return 1;
    unlink(PROVA_F);
    unlink(VISTA_F);
    return 0;
}

static int guardia(unsigned int secondi)
{
    unsigned int t = 0;

    if (access(PROVA_F, F_OK) != 0) return 0;          /* nothing on trial */
    printf("svga: un modo video e' in prova; senza conferma entro %u s torna quello di prima.\n",
           secondi);
    for (;;) {
        sleep(1);
        t++;
        if (access(PROVA_F, F_OK) != 0) return 0;      /* confirmed, or put back */
        /* The desktop asked: its own 30 seconds decide. The guard stays as
         * a last net, five minutes in all. */
        if (access(VISTA_F, F_OK) == 0 ? t >= 300 : t >= secondi) break;
    }
    if (annulla() != 0) return 1;
    printf("svga: riavvio nel modo di prima.\n");
    reboot(EXOS_RB_RESTART);
    return 1;
}

int main(int argc, char **argv)
{
    const char *voluto = NULL;
    int i, modo, prova = 0, opt_lista = 0;
    unsigned int w = 0, h = 0;

    for (i = 1; i < argc; i++) {
        /* La convenzione comune a tutti i driver: sonda, di' cosa hai
         * trovato, esci con 0 se servi su questa macchina.
         *
         * ! QUI LA RISPOSTA E' SEMPRE SI', E VA DETTO PERCHE'. Non c'e'
         * hardware da sondare: la risoluzione la si sceglie, non la si
         * trova. */
        if (strcmp(argv[i], "-i") == 0) {
            printf("svga: sceglie la risoluzione della console.\n");
            printf("      Nessuna periferica propria: la modalita' la\n");
            printf("      imposta Stage 2. Serve su qualunque macchina.\n");
            return 0;
        }
        if      (strcmp(argv[i], "-n") == 0) opt_n = 1;
        else if (strcmp(argv[i], "-16") == 0) opt_16 = 1;
        else if (strcmp(argv[i], "-lista") == 0) opt_lista = 1;
        else if (strcmp(argv[i], "-prova") == 0) prova = 1;
        else if (strcmp(argv[i], "-conferma") == 0) {
            unlink(PROVA_F);
            unlink(VISTA_F);
            printf("svga: il modo resta questo.\n");
            return 0;
        }
        else if (strcmp(argv[i], "-annulla") == 0) {
            if (carica_stage2() != 0) return 1;
            return annulla();
        }
        else if (strcmp(argv[i], "-guardia") == 0) {
            unsigned int s = (i + 1 < argc) ? (unsigned int)atoi(argv[i + 1]) : 0;

            if (access(PROVA_F, F_OK) != 0) return 0;
            if (carica_stage2() != 0) return 1;
            return guardia(s ? s : 120);
        }
        else if (strcmp(argv[i], "-h") == 0 ||
                 strcmp(argv[i], "-help") == 0 ||
                 strcmp(argv[i], "--help") == 0) { uso(); return 0; }
        else if (argv[i][0] == '-') { uso(); return 1; }
        else voluto = argv[i];
    }

    if (opt_lista) { lista(); return 0; }
    if (carica_stage2() != 0) return 1;

    if (voluto == NULL) { mostra(); return 0; }

    modo = modo_da_nome(voluto);
    if (modo < 0) {
        printf("svga: '%s' non e' una risoluzione che conosco.\n", voluto);
        printf("      Sono: testo, 640x480, 800x600, 1024x768, o LARGHEZZAxALTEZZA\n");
        printf("      fra quelle di `/dev/svga.drv -lista`.\n");
        return 1;
    }
    if (modo == 4) {
        leggi_wxh(voluto, &w, &h);
        if (!ha_cust()) {
            printf("svga: questo Stage 2 conosce solo testo, 640x480, 800x600 e 1024x768.\n");
            printf("      Gli altri modi arrivano con il bootloader nuovo (`install`).\n");
            return 1;
        }
    }
    if (modo > 0) {
        unsigned int lw = modo == 4 ? w : (unsigned int)atoi(nome_modo[modo]);
        unsigned int lh = modo == 4 ? h : (unsigned int)atoi(strchr(nome_modo[modo], 'x') + 1);
        int          o  = offerto(lw, lh, opt_16);

        if (o == 0) {
            printf("svga: la scheda non offre %ux%u%s. Quelli che ha: /dev/svga.drv -lista\n",
                   lw, lh, opt_16 ? " a 16 bit" : "");
            return 1;
        }
        if (o < 0 && modo == 4)
            printf("svga: (la lista dei modi non e' nota: avviato in testo. Se la scheda\n"
                   "      non ha %ux%u, Stage 2 ripieghera' sulla console di testo.)\n", lw, lh);
    }
    if (modo == 4) snprintf(g_nome_cust, sizeof(g_nome_cust), "%ux%u", w, h);

    printf("Risoluzione: %s%s%s\n\n", modo == 4 ? g_nome_cust : nome_modo[modo],
           modo > 0 && modo < 4 ? "" : "", opt_16 ? ", 16 bit" : "");
    printf("  %s        voce svga\n", g_cfg);
    printf("  %s        byte alla firma 'SVGAMODE' (offset %ld)\n",
           g_s2, pos_firma + OFF_MODO);

    if (opt_n) { printf("\n(-n: non ho scritto niente)\n"); return 0; }

    /* ! SI AVVISA PRIMA DI SCRIVERE, non dopo. Vedi radice_in_ram(). */
    if (radice_in_ram()) {
        printf("\n  ====================================================\n");
        printf("   ! LA RADICE STA IN RAM, E QUELLO CHE SCRIVO QUI\n");
        printf("     SPARISCE ALLO SPEGNIMENTO.\n");
        printf("   Per un cambio che RESTA si costruisce l'immagine gia' con\n");
        printf("   la risoluzione voluta.  make test-aa3k SVGA=800x600\n");
        printf("  ====================================================\n");
    }

    if (prova) {
        if (prova_scrivi(modo == 4 ? g_nome_cust : nome_modo[modo]) != 0) return 1;
        guardia_in_avvio();
    }
    if (scrivi_modo(modo, w, h, opt_16, modo == 4 ? g_nome_cust : nome_modo[modo]) != 0) {
        if (prova) unlink(PROVA_F);
        return 1;
    }

    /* ! IL RIAVVIO SI DICE FORTE. Chi ha appena scritto `/dev/svga.drv
     * 800x600` si aspetta di vedere lo schermo cambiare, e non cambia niente:
     * la modalita' la imposta Stage 2, che ha finito il proprio lavoro
     * all'accensione. */
    printf("\n  ====================================================\n");
    printf("   SERVE UN RIAVVIO perche' abbia effetto.\n");
    if (prova) {
        printf("   E' una PROVA: al riavvio la scrivania chiede conferma;\n");
        printf("   senza risposta torna il modo di prima da sola.\n");
    }
    printf("  ====================================================\n");
    printf("\n   reboot     riavvia adesso\n");
    return 0;
}

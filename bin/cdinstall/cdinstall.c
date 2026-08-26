/* =============================================================================
 * bin/cdinstall/cdinstall.c
 * EX-OS - Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * cdinstall - l'installatore del CD, a schermo intero
 *
 *     cdinstall            la procedura guidata
 *     cdinstall /disco     passa dritto a /bin/install (uso da script)
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' NON E' `install`, E PERCHE' STA SOLO SUL CD
 *
 * /bin/install sta sul FLOPPY, e il floppy e' 1,44 MB che non crescono mai. Il
 * kernel invece cresce a ogni cosa che impara: nella sola giornata del 26
 * agosto 2026 sono dovuti uscire dal floppy `libctest` e `hello` per far posto
 * a due programmi nuovi. Un installatore che continuasse ad arricchirsi la'
 * dentro sarebbe il prossimo a togliere spazio all'unica cosa che sul floppy
 * DEVE starci - il kernel.
 *
 * Quindi la divisione e' netta e definitiva:
 *
 *     /bin/install     CONGELATO. Copia i file, installa l'avvio, fonde
 *                      kernel.cfg. Basta a installare da un floppy e basta.
 *     /bin/cdinstall   tutto il resto, adesso e in futuro. Sta sul CD, e sul
 *                      CD lo spazio non e' un problema.
 *
 * ! E `install` GLI CEDE IL POSTO DA SOLO. Chi digita `install` ha in mente
 * l'installazione, non quale dei due programmi la faccia: se il CD c'e', si
 * ritrova questo senza aver dovuto sapere che esiste. Vedi la delega in testa
 * al main di install.c.
 *
 * ! MA LA COPIA DEI FILE NON SI RIFA' QUI. Quando c'e' da copiare, questo
 * programma chiama `install -diretto`, che e' l'unico posto dove quel codice
 * esiste - con la mappa dei settori, la fusione di kernel.cfg, i controlli
 * sulla contiguita' di stage2 e kernel. Riscriverlo qui vorrebbe dire due
 * installatori che divergono, e il secondo si accorgerebbe di essere rimasto
 * indietro il giorno che qualcuno non riesce ad avviare il disco.
 *
 * -----------------------------------------------------------------------------
 * ! L'INTERFACCIA E' TESTUALE E DISEGNATA A MANO, e non usa ExWin
 *
 * ExWin richiede il server grafico, il server grafico richiede una modalita'
 * VESA impostata da Stage 2, e Stage 2 la imposta solo se qualcuno ha gia'
 * scritto la configurazione - cioe' se il sistema e' gia' installato. Un
 * installatore che pretendesse la grafica non potrebbe girare esattamente
 * nell'unico momento in cui serve.
 *
 * Ottanta colonne per venticinque righe, sequenze ANSI che la console del
 * kernel sa gia' interpretare (CUP, ED, EL, SGR: vedi ansi_apply_csi in
 * kernel/arch/x86/vga.c), e i tasti presi uno per uno dal servizio 'kbd'.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `cdinstall -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("cdinstall", "0.001");

/* =============================================================================
 * ! LA MISURA DELLO SCHERMO SI CHIEDE, NON SI SCRIVE QUI.
 *
 * Ottanta per venticinque e' vero su una console di TESTO e falso su una
 * console grafica: a 800x600 con il carattere 8x16 sono cento colonne per
 * trentasette righe. Scritta a mano, la barra dei tasti finiva a meta' schermo
 * con dodici righe di nero sotto, e il fondo blu non arrivava in fondo.
 *
 * TTY_IOCTL_GETSIZE la risponde esatta in tutt'e due i casi. I valori qui sono
 * il ripiego per quando l'ioctl non risponde — un terminale che non e' una
 * console — e non la misura attesa.
 * ========================================================================== */
#define RIGHE_MIN   25
#define COLONNE_MAX 200

static int g_righe = 25;
static int g_cols  = 80;

static void sc_misura(void)
{
    TtyWinSize ws;

    if (ioctl(1, TTY_IOCTL_GETSIZE, &ws) == 0 && ws.rows > 0 && ws.cols > 0) {
        g_righe = ws.rows;
        g_cols  = ws.cols;
    }
    if (g_righe < RIGHE_MIN)     g_righe = RIGHE_MIN;
    if (g_cols  > COLONNE_MAX)   g_cols  = COLONNE_MAX;
}

/* =============================================================================
 * LO SCHERMO
 *
 * ! SI SCRIVE CON write(1, ...) E NON CON printf, e non e' pignoleria: printf
 * passa da un buffer e da una formattazione che qui non servono, e soprattutto
 * spezza le sequenze di controllo se il buffer si riempie a meta' di una. Una
 * sequenza ANSI spezzata non e' un carattere storto: e' meta' sequenza
 * stampata come testo e il resto interpretato a caso.
 * ========================================================================== */
static void o(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    if (n) write(1, s, n);
}

static void o_n(int v)
{
    char b[12];
    int  i = 0, j;

    if (v == 0) { o("0"); return; }
    if (v < 0)  { o("-"); v = -v; }
    while (v > 0 && i < 11) { b[i++] = (char)('0' + v % 10); v /= 10; }
    for (j = i - 1; j >= 0; j--) write(1, &b[j], 1);
}

static void sc_va(int r, int c)
{
    o("\033["); o_n(r); o(";"); o_n(c); o("H");
}

/* I colori sono quelli del testo VGA, per numero SGR. Il fondo blu e' lo
 * stesso della scrivania: chi ha gia' visto ExWin riconosce la casa. */
#define C_NORM   "\033[0m"
#define C_TIT    "\033[1;37;44m"    /* bianco su blu, in grassetto */
#define C_BOX    "\033[0;37;44m"
#define C_SCELTO "\033[0;30;47m"    /* nero su bianco: la riga scelta */
#define C_AVVISO "\033[1;33;44m"
#define C_ERR    "\033[1;31;44m"

static void sc_pulisci(void)
{
    o(C_BOX);
    o("\033[2J");
    sc_va(1, 1);
}

static void sc_scrivi(int r, int c, const char *s)
{
    sc_va(r, c);
    o(s);
}

/* Una riga piena di un carattere: serve alle cornici e a cancellare. */
static void sc_ripeti(int r, int c, char ch, int n)
{
    char b[COLONNE_MAX + 1];
    int  i;

    if (n > g_cols) n = g_cols;
    for (i = 0; i < n; i++) b[i] = ch;
    b[n] = '\0';
    sc_scrivi(r, c, b);
}

/* ! LA CORNICE E' FATTA DI ASCII, NON DI CARATTERI SEMIGRAFICI. La console e'
 * in code page 437 e i caratteri di cornice ci sarebbero, ma la stessa schermata
 * finisce anche sulla SERIALE - che e' il modo in cui si guarda un'installazione
 * che non va - e li' diventerebbero simboli casuali. Piu' brutta e leggibile
 * dappertutto batte piu' bella e illeggibile dove serve. */
static void sc_riquadro(int r, int c, int w, int h, const char *titolo)
{
    int i;

    sc_scrivi(r, c, "+");
    sc_ripeti(r, c + 1, '-', w - 2);
    sc_scrivi(r, c + w - 1, "+");

    for (i = 1; i < h - 1; i++) {
        sc_scrivi(r + i, c, "|");
        sc_ripeti(r + i, c + 1, ' ', w - 2);
        sc_scrivi(r + i, c + w - 1, "|");
    }

    sc_scrivi(r + h - 1, c, "+");
    sc_ripeti(r + h - 1, c + 1, '-', w - 2);
    sc_scrivi(r + h - 1, c + w - 1, "+");

    if (titolo && titolo[0]) {
        int n = 0;
        while (titolo[n]) n++;
        sc_scrivi(r, c + (w - n - 2) / 2, " ");
        o(titolo);
        o(" ");
    }
}

static void sc_testata(const char *sotto)
{
    sc_pulisci();
    o(C_TIT);
    sc_ripeti(1, 1, ' ', g_cols);
    sc_scrivi(1, 3, "EX-OS - installazione");
    if (sotto) { sc_va(1, 32); o("| "); o(sotto); }
    o(C_BOX);
}

static void sc_barra(const char *tasti)
{
    /* ! L'ULTIMA CELLA DELL'ULTIMA RIGA NON SI TOCCA MAI. Scrivendoci dentro,
     * il cursore va a capo — e in fondo allo schermo «a capo» vuol dire far
     * SCORRERE tutto di una riga. Il sintomo era la testata che spariva ogni
     * volta che si ridisegnava la barra dei tasti, cioe' a ogni schermata: si
     * vedeva il risultato e non la causa, perche' la riga che scompare sta
     * dalla parte opposta di quella che la fa scomparire. */
    o(C_TIT);
    sc_ripeti(g_righe, 1, ' ', g_cols - 1);
    sc_scrivi(g_righe, 3, tasti);
    o(C_BOX);
}

/* =============================================================================
 * LA TASTIERA, UN TASTO PER VOLTA
 *
 * ! LA MODALITA' SI RIAFFERMA A OGNI SCADENZA, non si rinuncia. Chi si ferma a
 * leggere una schermata - che qui e' la cosa normale, non l'eccezione -
 * perderebbe il modo raw, e i tasti successivi finirebbero in una riga che
 * nessuno legge. Stessa struttura di bin/help/help.c e bin/sh/shell.c.
 *
 * ! E SE IL SERVIZIO 'kbd' NON C'E', SI RINUNCIA ALL'INTERFACCIA, non ai
 * tasti: senza modo raw non si leggono le frecce, e un menu che non si puo'
 * muovere e' peggio di una domanda scritta. Vedi main().
 * ========================================================================== */
static int      g_kbd = -1;
static unsigned g_console = 0;

static void kbd_modo(unsigned modo)
{
    KbdSetMode m;

    if (g_kbd <= 0) return;
    m.modo    = modo;
    m.console = g_console;
    ipc_send((unsigned)g_kbd, KBD_MSG_SETMODE, &m, sizeof(m));
}

static unsigned tasto(void)
{
    IpcMessage meta;
    unsigned   k = 0;
    int        i;

    if (g_kbd <= 0) return 0;

    for (;;) {
        if (ipc_send((unsigned)g_kbd, KBD_MSG_READKEY,
                     &g_console, sizeof(g_console)) < 0) return 0;

        for (i = 0; i < 8; i++) {
            if (ipc_recv_timeout(&meta, &k, sizeof(k), 2000) < 0) break;
            if ((int)meta.sender_pid != g_kbd) continue;
            if (meta.tipo == KBD_MSG_KEY && meta.len >= sizeof(k))
                return k & KBD_KEY_MASK;
        }
        kbd_modo(KBD_MODE_RAW);
    }
}

/* =============================================================================
 * UN MENU: frecce per scegliere, Invio per confermare, Esc per tornare
 *
 * ! LA RIGA SCELTA SI VEDE DAL COLORE PIENO, non da un segno accanto. Un
 * asterisco davanti alla voce si perde fra il testo; una riga in negativo si
 * trova con la coda dell'occhio, ed e' l'unica informazione che serve a chi sta
 * premendo una freccia.
 *
 * Rende l'indice scelto, oppure -1 se si e' premuto Esc.
 * ========================================================================== */
static int menu(int r, int c, int w, const char *const *voci, int n, int scelto)
{
    for (;;) {
        int i;

        for (i = 0; i < n; i++) {
            o(i == scelto ? C_SCELTO : C_BOX);
            sc_ripeti(r + i, c, ' ', w);
            sc_scrivi(r + i, c + 1, voci[i]);
        }
        o(C_BOX);
        sc_va(g_righe - 1, 1);

        switch (tasto()) {
        case KBD_K_UP:   if (scelto > 0)     scelto--; break;
        case KBD_K_DOWN: if (scelto < n - 1) scelto++; break;
        case KBD_K_HOME: scelto = 0;     break;
        case KBD_K_END:  scelto = n - 1; break;
        case '\r': case '\n': return scelto;
        case 0x1B: return -1;
        case 0: return -1;              /* la tastiera non risponde piu' */
        default: break;
        }
    }
}

/* =============================================================================
 * UN NUMERO, dentro una riga che si vede cambiare
 *
 * ! SI PARTE DAL VALORE PROPOSTO E LO SI PUO' CANCELLARE, invece di trovare il
 * campo vuoto: chi va bene com'e' preme Invio e basta, e chi vuole un altro
 * numero se ne accorge da solo che deve prima cancellare. Un campo vuoto con
 * accanto scritto «(predefinito 64)» costringe tutti a leggere una parentesi.
 * ========================================================================== */
static int chiedi_numero(int r, int c, int w, int valore, int massimo)
{
    char b[12];
    int  n = 0, i;

    /* il valore proposto, gia' scritto dentro il campo */
    if (valore > 0) {
        char t[12];
        int  v = valore, j = 0;
        while (v > 0 && j < 11) { t[j++] = (char)('0' + v % 10); v /= 10; }
        while (j > 0) b[n++] = t[--j];
    }
    b[n] = '\0';

    for (;;) {
        unsigned k;

        o(C_SCELTO);
        sc_ripeti(r, c, ' ', w);
        sc_scrivi(r, c + 1, b);
        o(C_BOX);
        sc_va(r, c + 1 + n);

        k = tasto();
        if (k == '\r' || k == '\n') {
            if (n == 0) return -1;
            i = atoi(b);
            if (i <= 0 || i > massimo) {
                /* ! SI DICE QUAL E' IL MASSIMO, non «valore non valido». La
                 * seconda frase costringe a indovinare due volte. */
                o(C_ERR);
                sc_scrivi(r + 1, c, "al massimo ");
                o_n(massimo); o(" MB   ");
                o(C_BOX);
                continue;
            }
            sc_ripeti(r + 1, c, ' ', w + 12);
            return i;
        }
        if (k == 0x1B || k == 0) return -1;
        if ((k == '\b' || k == 0x7F) && n > 0) { b[--n] = '\0'; continue; }
        if (k >= '0' && k <= '9' && n < 6) { b[n++] = (char)k; b[n] = '\0'; }
    }
}

/* Si' oppure no, con le frecce. Rende 1, 0, oppure -1 per Esc. */
static int chiedi_si(int r, int c, const char *domanda, int predefinito)
{
    static const char *const SN[2] = { "  No", "  Si" };
    int s;

    sc_scrivi(r, c, domanda);
    s = menu(r + 1, c + 2, 12, SN, 2, predefinito ? 1 : 0);
    return s;
}

/* =============================================================================
 * I DISCHI
 * ========================================================================== */
#define DISCHI_MAX 4

typedef struct {
    unsigned int idx;
    unsigned int mb;
    unsigned int n_part;
    char         nome[8];
    char         riga[72];
} Disco;

static Disco g_disco[DISCHI_MAX];
static int   g_n_dischi = 0;

static void mb_in(char *dst, unsigned int mb)
{
    /* GB quando i MB diventano tanti: quattro cifre di megabyte non le legge
     * nessuno, e un disco si ricorda in gigabyte. */
    if (mb >= 10240) {
        int g = (int)(mb / 1024);
        int d = (int)((mb % 1024) * 10 / 1024);
        sprintf(dst, "%d,%d GB", g, d);
    } else {
        sprintf(dst, "%u MB", mb);
    }
}

static void cerca_dischi(void)
{
    DiskInfo di;
    unsigned int i;

    g_n_dischi = 0;
    for (i = 0; i < DISCHI_MAX; i++) {
        char misura[16];

        if (diskinfo(i, &di) != 0 || !di.presente) continue;
        if (di.tipo != 1) continue;                 /* solo dischi, non CD */
        if (di.settori_hi != 0) continue;           /* oltre 2 TiB: non qui */

        g_disco[g_n_dischi].idx = i;
        g_disco[g_n_dischi].mb  =
            (unsigned int)((unsigned long long)di.settori_lo * 512ull
                           / (1024ull * 1024ull));
        g_disco[g_n_dischi].n_part = di.n_part;
        sprintf(g_disco[g_n_dischi].nome, "hd%u", i);

        mb_in(misura, g_disco[g_n_dischi].mb);
        sprintf(g_disco[g_n_dischi].riga, "hd%u   %-10s  %-28s  %u partizioni",
                i, misura, di.modello, di.n_part);
        g_n_dischi++;
    }
}

/* =============================================================================
 * LE MISURE - una schermata sola, e lo spazio che avanza si vede cambiare
 *
 * ! I TRE NUMERI STANNO INSIEME PERCHE' SONO UNA DECISIONE SOLA. Chiesti uno
 * per volta, chi risponde al primo non sa ancora cosa gli restera' per il
 * terzo, e scopre di aver sbagliato quando non puo' piu' tornare indietro. Qui
 * si vede il totale, si vede l'avanzo, e si cambia quello che si vuole finche'
 * non torna.
 * ========================================================================== */
typedef struct {
    unsigned int sis, scambio, dati, totale;
} Misure;

static void misure_disegna(const Misure *m, int scelto, int avanzo)
{
    static const char *const ETICHETTA[3] = {
        "Sistema  (ext2, avviabile)",
        "Scambio  (memoria virtuale)",
        "Dati     (ext2)"
    };
    const unsigned int *v[3];
    int i;

    v[0] = &m->sis; v[1] = &m->scambio; v[2] = &m->dati;

    for (i = 0; i < 3; i++) {
        o(i == scelto ? C_SCELTO : C_BOX);
        sc_ripeti(9 + i * 2, 8, ' ', 62);
        sc_scrivi(9 + i * 2, 10, ETICHETTA[i]);
        sc_va(9 + i * 2, 52);
        if (*v[i] == 0) o("     -");
        else { o_n((int)*v[i]); o(" MB"); }
    }

    o(C_BOX);
    sc_ripeti(16, 8, ' ', 62);
    sc_scrivi(16, 10, "Non assegnato: ");
    if (avanzo < 0) o(C_ERR); else o(C_AVVISO);
    o_n(avanzo); o(" MB    ");
    o(C_BOX);
}

/* Rende 1 se si va avanti, 0 se si torna indietro. */
static int schermata_misure(Misure *m)
{
    int scelto = 0;

    sc_testata("le misure");
    sc_riquadro(4, 4, 72, 16, "Come dividere il disco");
    sc_scrivi(6, 8, "Frecce per scegliere, cifre per cambiare, Invio quando va bene.");
    sc_scrivi(7, 8, "Zero MB vuol dire: non fare quella partizione.");
    sc_barra(" Frecce muovono   0-9 cambiano   Invio conferma   Esc torna indietro ");

    for (;;) {
        int avanzo = (int)m->totale - (int)(m->sis + m->scambio + m->dati);
        unsigned k;

        misure_disegna(m, scelto, avanzo);
        sc_va(g_righe - 1, 1);

        k = tasto();
        if (k == KBD_K_UP   && scelto > 0) { scelto--; continue; }
        if (k == KBD_K_DOWN && scelto < 2) { scelto++; continue; }
        if (k == 0x1B || k == 0) return 0;

        if (k == '\r' || k == '\n') {
            if (m->sis == 0) {
                o(C_ERR);
                sc_scrivi(18, 10, "Senza partizione di sistema non c'e' niente da installare.");
                o(C_BOX);
                continue;
            }
            if (avanzo < 0) {
                o(C_ERR);
                sc_scrivi(18, 10, "Le tre misure superano il disco: toglierne da qualcuna.  ");
                o(C_BOX);
                continue;
            }
            sc_ripeti(18, 10, ' ', 60);
            return 1;
        }

        if (k >= '0' && k <= '9') {
            /* ! SI RIENTRA NEL CAMPO CON LA CIFRA GIA' BATTUTA, invece di
             * ignorarla e aspettare che si ricominci: chi ha cominciato a
             * scrivere un numero ha gia' deciso, e perdergli il primo tasto
             * e' il modo piu' sicuro di fargli scrivere il numero sbagliato. */
            unsigned int *campo = (scelto == 0) ? &m->sis
                                : (scelto == 1) ? &m->scambio : &m->dati;
            unsigned int  altri = m->sis + m->scambio + m->dati - *campo;
            int           v;

            *campo = (unsigned int)(k - '0');
            v = chiedi_numero(9 + scelto * 2, 50, 12, (int)*campo,
                              (int)(m->totale - altri));
            if (v > 0) *campo = (unsigned int)v;
        }
    }
}

/* =============================================================================
 * IL LAVORO - e qui si ESCE dall'interfaccia, di proposito
 *
 * ! DA QUI IN POI SI TORNA A UNA SCHERMATA CHE SCORRE. mkfs, mkswap e install
 * scrivono ognuno le proprie righe, e sono righe che vale la pena leggere:
 * quanti inode, quanti blocchi, quale file e' stato copiato. Tenerli dentro un
 * riquadro vorrebbe dire o nasconderli - e allora l'installazione diventa una
 * barra che avanza senza dire niente - o ritagliarli a mano, cioe' riscrivere
 * qui cosa ognuno di loro ha il diritto di dire.
 *
 * ! E SE QUALCOSA VA STORTO, CIO' CHE RESTA A SCHERMO E' IL MESSAGGIO VERO di
 * chi ha fallito, non un «errore durante l'installazione» che costringe a
 * indovinare quale dei tre passi fosse.
 * ========================================================================== */
static int lancia(const char *perc, char *const *v)
{
    int pid, stato = 0;

    pid = spawn(perc, v);
    if (pid < 0) {
        printf("cdinstall: non riesco ad avviare %s\n", perc);
        return -1;
    }
    /* Il primo piano va a lui: senza, ogni sua domanda leggerebbe zero byte e
     * si risponderebbe da sola con il valore predefinito. */
    console_setfg((unsigned int)pid);
    waitpid(pid, &stato, 0);
    console_setfg((unsigned int)getpid());
    return stato;
}

static const char *trova_prog(const char *a, const char *b)
{
    if (access(a, F_OK) == 0) return a;
    if (access(b, F_OK) == 0) return b;
    return 0;
}

static int prepara(const Disco *d, const Misure *m,
                   char *p_sis, char *p_swap, char *p_dati)
{
    PartTabella  tab;
    unsigned int lba = 2048u;       /* 1 MiB: vedi sotto */
    int          i = 0;
    const char  *mkfs, *mkswap;
    char        *v[6];

    /* ! LE MISURE SI ALLINEANO A UN MEGABYTE, e non e' superstizione: un disco
     * a settori da 4 KB paga ogni scrittura disallineata con una
     * lettura-modifica-scrittura. Allineare costa al massimo un megabyte. */
    memset(&tab, 0, sizeof(tab));

    tab.voce[i].attiva     = 0x80;      /* l'avvio sta qui */
    tab.voce[i].tipo       = 0x83;      /* Linux: ci va ext2 */
    tab.voce[i].inizio_lo  = lba;
    tab.voce[i].settori_lo = m->sis * 2048u;
    lba += tab.voce[i].settori_lo;
    sprintf(p_sis, "%sp%d", d->nome, i + 1);
    i++;

    p_swap[0] = '\0';
    if (m->scambio > 0) {
        /* ! IL TIPO 0x82 IL KERNEL DI EX-OS NON LO GUARDA - si fida solo della
         * firma che ci mette mkswap - ma si scrive lo stesso: e' quello che
         * dice a Linux, a un fdisk qualunque e a chi rimettera' le mani su
         * questo disco fra un anno che li' dentro non c'e' un filesystem. */
        tab.voce[i].tipo       = 0x82;
        tab.voce[i].inizio_lo  = lba;
        tab.voce[i].settori_lo = m->scambio * 2048u;
        lba += tab.voce[i].settori_lo;
        sprintf(p_swap, "%sp%d", d->nome, i + 1);
        i++;
    }

    p_dati[0] = '\0';
    if (m->dati > 0) {
        tab.voce[i].tipo       = 0x83;
        tab.voce[i].inizio_lo  = lba;
        tab.voce[i].settori_lo = m->dati * 2048u;
        sprintf(p_dati, "%sp%d", d->nome, i + 1);
        i++;
    }

    printf("\n[1/4] La tabella delle partizioni di %s...\n", d->nome);
    if (partwrite(d->idx, &tab) != 0) {
        printf("cdinstall: il kernel ha rifiutato la tabella (problemi 0x%x).\n",
               tab.problemi);
        printf("           Se una partizione di %s e' montata, smontala prima.\n",
               d->nome);
        return -1;
    }
    printf("      scritta, e i dispositivi sono gia' aggiornati.\n");

    mkfs   = trova_prog("/bin/mkfs", "/cdrom/bin/mkfs");
    mkswap = trova_prog("/bin/mkswap", "/cdrom/bin/mkswap");

    printf("\n[2/4] Il filesystem su %s...\n", p_sis);
    if (!mkfs) { printf("cdinstall: mkfs non si trova.\n"); return -1; }
    v[0] = (char *)mkfs; v[1] = "-t"; v[2] = "ext2"; v[3] = "-f";
    v[4] = p_sis; v[5] = 0;
    if (lancia(mkfs, v) != 0) {
        printf("cdinstall: %s non si e' formattata.\n", p_sis);
        return -1;
    }

    if (p_dati[0]) {
        printf("\n      e su %s...\n", p_dati);
        v[4] = p_dati;
        if (lancia(mkfs, v) != 0)
            printf("cdinstall: %s non si e' formattata: si puo' rifare a mano.\n",
                   p_dati);
    }

    printf("\n[3/4] L'area di scambio...\n");
    if (p_swap[0]) {
        if (!mkswap) {
            printf("cdinstall: mkswap non si trova: niente memoria virtuale.\n");
            p_swap[0] = '\0';
        } else {
            v[0] = (char *)mkswap; v[1] = "-f"; v[2] = p_swap; v[3] = 0;
            if (lancia(mkswap, v) != 0) {
                printf("cdinstall: %s non si e' preparata.\n", p_swap);
                p_swap[0] = '\0';
            }
        }
    } else {
        printf("      non richiesta.\n");
    }
    return 0;
}

/* =============================================================================
 * IL FILO: benvenuto, disco, misure, riepilogo, lavoro
 * ========================================================================== */
static int schermata_benvenuto(void)
{
    static const char *const SCELTE[2] = {
        "  Preparare un disco e installare EX-OS",
        "  Uscire"
    };

    sc_testata("benvenuto");
    sc_riquadro(4, 4, 72, 14, "EX-OS");
    sc_scrivi(6,  8, "Questa procedura prepara un disco e ci installa EX-OS.");
    sc_scrivi(8,  8, "Chiedera' prima come dividere il disco - quanto al sistema,");
    sc_scrivi(9,  8, "quanto alla memoria virtuale, quanto ai dati - poi mostrera'");
    sc_scrivi(10, 8, "il piano, e SOLO DOPO una conferma tocchera' qualcosa.");
    sc_scrivi(12, 8, "Fino a quel momento si puo' tornare indietro con Esc.");
    sc_barra(" Frecce muovono   Invio conferma   Esc esce ");

    return menu(15, 8, 62, SCELTE, 2, 0) == 0;
}

static int schermata_disco(int *scelto)
{
    const char *voci[DISCHI_MAX];
    int i, s;

    sc_testata("il disco");
    sc_riquadro(4, 4, 72, 12, "Su quale disco");

    if (g_n_dischi == 0) {
        o(C_ERR);
        sc_scrivi(7, 8, "Non c'e' nessun disco fisso su questa macchina.");
        o(C_BOX);
        sc_scrivi(9, 8, "EX-OS si installa su un disco ATA. Un CD non si scrive e");
        sc_scrivi(10, 8, "un floppy non ha spazio.");
        sc_barra(" Un tasto per uscire ");
        tasto();
        return 0;
    }

    sc_scrivi(6, 8, "Tutto il contenuto del disco scelto andra' perso.");
    for (i = 0; i < g_n_dischi; i++) voci[i] = g_disco[i].riga;

    sc_barra(" Frecce muovono   Invio conferma   Esc torna indietro ");
    s = menu(8, 8, 62, voci, g_n_dischi, 0);
    if (s < 0) return 0;
    *scelto = s;
    return 1;
}

static int schermata_riepilogo(const Disco *d, const Misure *m)
{
    char linea[80];
    int  r = 8, i = 1;

    sc_testata("conferma");
    sc_riquadro(4, 4, 72, 15, "Ecco cosa succedera'");

    sprintf(linea, "Il disco %s (%u MB) diventera':", d->nome, d->mb);
    sc_scrivi(6, 8, linea);

    sprintf(linea, "  %sp%d   %5u MB   ext2, il sistema (avviabile)",
            d->nome, i++, m->sis);
    sc_scrivi(r++, 8, linea);
    if (m->scambio) {
        sprintf(linea, "  %sp%d   %5u MB   memoria virtuale", d->nome, i++,
                m->scambio);
        sc_scrivi(r++, 8, linea);
    }
    if (m->dati) {
        sprintf(linea, "  %sp%d   %5u MB   ext2, i dati", d->nome, i++, m->dati);
        sc_scrivi(r++, 8, linea);
    }

    o(C_AVVISO);
    sc_scrivi(r + 1, 8, "TUTTO CIO' CHE C'E' SU ");
    o(d->nome);
    o(" ANDRA' PERSO.");
    o(C_BOX);

    sc_barra(" Frecce muovono   Invio conferma   Esc torna indietro ");
    return chiedi_si(r + 3, 8, "Procedere?", 0) == 1;
}

/* ! LA COPIA LA FA `install`, E SI CHIAMA COL SUO NOME. Il perche' sta in
 * testa a questo file: quel codice esiste in un posto solo, ed e' quello. */
static int chiama_install(const char *punto, int argc, char **argv)
{
    const char *ins = trova_prog("/bin/install", "/cdrom/bin/install");
    char       *v[20];
    int         n = 0, k;

    if (!ins) {
        printf("cdinstall: /bin/install non si trova.\n");
        return -1;
    }

    v[n++] = (char *)ins;
    v[n++] = "-diretto";
    for (k = 1; k < argc && n < 18; k++) v[n++] = argv[k];
    if (punto) v[n++] = (char *)punto;
    v[n] = 0;

    return lancia(ins, v);
}

int main(int argc, char **argv)
{
    Disco  *d;
    Misure  m;
    int     idisco = 0;
    char    p_sis[16], p_swap[16], p_dati[16];
    char   *v[4];

    /* ! CON ARGOMENTI SI PASSA DRITTO, e non e' una scorciatoia: `install
     * /disco` deve continuare a funzionare uguale dentro uno script, e da
     * quando install delega a questo programma, quello script finisce QUI. Una
     * procedura guidata che si apre in mezzo a un'automazione la blocca su una
     * domanda che nessuno vedra'. */
    if (argc > 1) return chiama_install(0, argc, argv);

    {
        ConsoleInfo ci;

        g_kbd = ipc_lookup(KBD_SERVICE_NAME);
        if (console_info(&ci) == 0) g_console = ci.mia;
    }

    /* ! SENZA IL SERVIZIO 'kbd' NON C'E' INTERFACCIA, e si dice invece di
     * disegnare una schermata che non risponde ai tasti. Chi legge questo
     * messaggio ha ancora l'installatore vecchio, che funziona. */
    if (g_kbd <= 0) {
        printf("cdinstall: il servizio 'kbd' non risponde: niente procedura\n");
        printf("           guidata. Si installa con:  install /disco\n");
        return 1;
    }

    sc_misura();
    kbd_modo(KBD_MODE_RAW);
    cerca_dischi();

    if (!schermata_benvenuto())          goto esci;
    if (!schermata_disco(&idisco))       goto esci;

    d = &g_disco[idisco];

    /* Le proposte: lo scambio grande quanto serve a questa macchina - il
     * doppio della RAM, la regola vecchia, che regge ancora per il caso che
     * interessa: tenere in vita programmi piu' grandi della memoria. */
    {
        MemInfo mi;
        unsigned int libero = d->mb > 1 ? d->mb - 1 : 0;

        m.totale  = libero;
        m.scambio = 64;
        if (meminfo(&mi) == 0 && mi.total_kb > 0) {
            m.scambio = (mi.total_kb / 1024) * 2;
            if (m.scambio < 32)  m.scambio = 32;
            if (m.scambio > 512) m.scambio = 512;
        }
        if (m.scambio > libero / 4) m.scambio = libero / 4;

        m.sis  = libero - m.scambio;
        m.dati = 0;
    }

    for (;;) {
        if (!schermata_misure(&m))       goto esci;
        if (schermata_riepilogo(d, &m))  break;
    }

    /* Da qui in poi si scrive davvero, e si torna a una schermata che scorre. */
    kbd_modo(KBD_MODE_COOKED);
    sc_pulisci();
    o(C_NORM);
    printf("\n=== EX-OS - installazione su %s ===\n", d->nome);

    if (prepara(d, &m, p_sis, p_swap, p_dati) != 0) {
        printf("\ncdinstall: il disco NON e' stato preparato del tutto.\n");
        return 1;
    }

    printf("\n[4/4] Il sistema su %s...\n", p_sis);

    /* ! IL MONTAGGIO E' NOSTRO, e va disfatto se qualcosa va storto: lasciare
     * montata una partizione appena formattata vuol dire che il prossimo
     * tentativo si sente rispondere «e' in uso» e non capisce da chi. */
    if (mount(p_sis, "/disco", 0) != 0) {
        printf("cdinstall: non riesco a montare %s su /disco\n", p_sis);
        return 1;
    }

    v[0] = "/bin/install"; v[1] = "-diretto"; v[2] = "/disco"; v[3] = 0;
    (void)v;
    if (chiama_install("/disco", 1, argv) != 0) {
        printf("\ncdinstall: l'installazione non e' andata a buon fine.\n");
        umount("/disco");
        return 1;
    }
    umount("/disco");

    printf("\n=== Fatto ===\n");
    printf("  %s  il sistema\n", p_sis);
    if (p_swap[0]) printf("  %s  memoria virtuale, gia' dichiarata in kernel.cfg\n", p_swap);
    if (p_dati[0]) printf("  %s  i dati\n", p_dati);
    printf("\n  Togli il CD e riavvia con:  reboot\n");
    return 0;

esci:
    kbd_modo(KBD_MODE_COOKED);
    sc_pulisci();
    o(C_NORM);
    printf("cdinstall: niente e' stato toccato.\n");
    return 1;
}

/* =============================================================================
 * drivers/sis/sis.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * SiS M760GX: cambiare modalita' SENZA il BIOS
 *
 *     /dev/sis.drv -testo    rimette la console di testo 80x25
 *     /dev/sis.drv -800      rimette 800x600 a 32 bit
 *     /dev/sis.drv -i        la sonda: c'e' una SiS 6330?
 *     /dev/sis.drv -n        dice cosa scriverebbe, senza scrivere
 *
 * -----------------------------------------------------------------------------
 * ! A COSA SERVE DAVVERO, E NON E' CAMBIARE RISOLUZIONE
 *
 * La risoluzione la sceglie Stage 2 all'accensione, col BIOS, e va benissimo
 * cosi'. Quello che manca al progetto — sta scritto in DIREZIONE.md dal 12
 * agosto 2026 — e' RIMETTERE IL TESTO DA MODO PROTETTO, quando il server
 * grafico e' morto e lo schermo e' congelato su quello che c'era.
 *
 * kernel/arch/x86/vga_modo3.c quel lavoro lo fa gia' con i registri VGA
 * standard, e dichiara il proprio limite: «su hardware reale con una VESA
 * attiva questo codice rimette i registri VGA e PUO' NON BASTARE». Questo file
 * e' la meta' che mancava: i registri ESTESI della SiS, quelli che tengono la
 * scheda in modalita' lineare e che nessun registro standard raggiunge.
 *
 * -----------------------------------------------------------------------------
 * ! I VALORI NON SONO CALCOLATI: SONO LETTI DA UNA MACCHINA VERA
 *
 * Non c'e' nessun documento pubblico dei registri estesi di questa scheda. Ma
 * la modalita' la imposta il BIOS, e il BIOS lascia i registri nello stato che
 * serve saper riprodurre: sonda.drv li ha letti in modo testo e in 800x600 su
 * un Acer Aspire 3000, e la DIFFERENZA fra i due referti e' la ricetta.
 * tools/sonda2tab.py la trasforma in sis_tab.h.
 *
 * ! E SI PRENDONO SOLO QUELLI CHE CAMBIANO. Dei 256 registri del CRTC quasi
 * tutti sono uguali nelle due modalita' — riscriverli sarebbe lavoro inutile —
 * e parecchi sono di sola lettura o riportano stati che rileggerli non
 * riproduce. L'elenco degli estesi e' esattamente quello della sottrazione:
 * una misura, non un giudizio.
 *
 * -----------------------------------------------------------------------------
 * ! E SI SCRIVONO SOLO GLI ESTESI: LO STANDARD LO RIMETTE IL KERNEL
 *
 * Per tornare al testo questo file scrive SOLO i registri SiS — SR06 e oltre,
 * CR19 e oltre — e poi chiama modo_testo(), cioe' la syscall 245, che porta
 * kernel/arch/x86/vga_modo3.c a rimettere i registri VGA standard, il
 * CARATTERE nel piano 2, la TAVOLOZZA del DAC, e a far ricominciare la console
 * a scrivere a 0xB8000 invece che nel framebuffer.
 *
 * ! NON E' UNA DIVISIONE DI COMODO: E' QUELLO CHE HA SALVATO IL 10 SETTEMBRE.
 * Prima questo file riscriveva anche i banchi standard COL REFERTO, e il
 * referto dei sedici registri della tavolozza dell'attributo (AR00..AR0F) e'
 * SBAGLIATO: sonda.drv li rilegge con il bit 5 dell'indice acceso — che e'
 * l'unico modo di non spegnere lo schermo mentre si legge tutto il resto — ma
 * con quel bit acceso la tavolozza interna NON e' leggibile dalla CPU, e la
 * scheda rende sempre lo stesso byte. Nel referto sono sedici 04 in fila, in
 * tutte e due le modalita'.
 *
 * Riscritti, quei sedici 04 mandano TUTTI i colori del testo — sfondo e
 * inchiostro — sull'indice 4 del DAC, che nella tavolozza VGA e' il rosso.
 * Lo schermo diventa rosso pieno e non c'e' piu' un carattere leggibile: la
 * macchina e' viva e sembra bloccata. E' successo esattamente cosi'.
 *
 * I registri standard non hanno bisogno di essere letti da questa scheda: sono
 * gli stessi dal 1987 e il kernel ce li ha gia' giusti. Del referto serve solo
 * la meta' che il kernel non puo' sapere.
 *
 * -----------------------------------------------------------------------------
 * ! COSA NON FA, DETTO SUBITO
 *
 *   - vale per QUESTA scheda e QUESTO pannello. Un altro portatile SiS vuole i
 *     suoi referti: il programma e' generale, la tabella no.
 *   - NON TOCCA IL PONTE VIDEO, di suo, e percio' il testo torna in un
 *     angolo dello schermo invece che pieno: la scalatura del pannello LCD
 *     resta impostata per la modalita' di prima. Si legge benissimo, ed e'
 *     quello che serve. `-ponte` prova a sistemarla, e vedi sotto perche'
 *     non e' acceso di suo.
 *   - non riprogramma l'orologio, e non serve: su questo portatile SR22..SR2F
 *     sono IDENTICI nelle due modalita'. Il pannello LCD ha una temporizzazione
 *     fissa e la scheda ci scala sopra l'immagine — su un monitor esterno la
 *     storia sarebbe un'altra.
 * ============================================================================= */

#include "libc.h"
#include "sis_tab.h"
#include "ponte_tab.h"

/* +0.001 a ogni modifica: `sis.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("sis.drv", "0.012");

#define VGA_BASE      0x3B0
#define VGA_PORTE     0x30

#define MISC_W        0x3C2
#define MISC_R        0x3CC
#define SEQ_IDX       0x3C4
#define SEQ_DAT       0x3C5
#define GC_IDX        0x3CE
#define GC_DAT        0x3CF
#define AC_IDX        0x3C0
#define AC_DAT        0x3C1
#define CRTC_IDX      0x3D4
#define CRTC_DAT      0x3D5
#define STATO         0x3DA

#define SIS_VENDITORE 0x1039
#define SIS_6330      0x6330

#define PCI_INDIRIZZO 0xCF8
#define PCI_DATO      0xCFC

static int g_prova = 0;      /* -n: dice e non scrive */

/* =============================================================================
 * L'USCITA: a schermo, e su file quando c'e' un file
 *
 * ! SU UNA MACCHINA CHE SI STA STUDIANDO LO SCHERMO E' IL TESTIMONE MENO
 * AFFIDABILE CHE CI SIA, perche' e' proprio la cosa sotto esame. Ogni prova di
 * questi giorni e' finita con qualcuno che ricopiava a mano numeri da uno
 * schermo storto — e ricopiando si sbaglia, si salta una riga, e la riga
 * saltata e' sempre quella che contava.
 *
 * dico() scrive nel file e a schermo. printf() resta per quello che ha senso
 * solo a schermo: le figure di prova.
 * ========================================================================== */
#define BUF_DIM   2048
static char         g_buf[BUF_DIM];
static unsigned int g_usati = 0;
static int          g_fd = -1;

static void svuota(void)
{
    if (g_fd < 0 || g_usati == 0) return;
    write(g_fd, g_buf, g_usati);
    g_usati = 0;
}

static void dico(const char *fmt, ...)
{
    char riga[512];
    int  n;
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    n = vsnprintf(riga, sizeof(riga), fmt, ap);
    __builtin_va_end(ap);

    if (n <= 0) return;
    if (n > (int)sizeof(riga) - 1) n = (int)sizeof(riga) - 1;

    write(1, riga, (unsigned int)n);

    if (g_fd < 0) return;
    if (g_usati + (unsigned int)n > BUF_DIM) svuota();
    memcpy(g_buf + g_usati, riga, (unsigned int)n);
    g_usati += (unsigned int)n;
}
static int g_ponte = 0;      /* -ponte: provare anche la scalatura del pannello */
static int g_ritocco = 0;    /* -ritocco: mettere il modo 3 di QUESTO BIOS */
static int g_specchi = 1;    /* -senzaspecchi: non allineare il ponte al CRTC */
static unsigned int g_finestra = 0;   /* base della finestra rilocata, 0 = non c'e' */

static unsigned int cfg_leggi(unsigned int bus, unsigned int slot,
                              unsigned int fn, unsigned int off)
{
    unsigned int ind = 0x80000000u | (bus << 16) | (slot << 11) |
                       (fn << 8) | (off & 0xFC);
    unsigned int val;

    if (ioport_out32(PCI_INDIRIZZO, ind) != 0) return 0xFFFFFFFFu;
    if (ioport_in32(PCI_DATO, &val) != 0)      return 0xFFFFFFFFu;
    return val;
}

/* Cerca la 1039:6330 e rende la sua BAR a porte, 0 se non la trova.
 *
 * ! LA SI CERCA, NON SI SCRIVE IL NUMERO. Su questo portatile la finestra sta
 * a 0xa000 e la scheda a 01:00.0, ma sono cose che il BIOS decide all'avvio:
 * un indirizzo fisso qui dentro sarebbe un driver che funziona finche' non si
 * cambia una scheda di posto. */
static unsigned int trova_finestra(void)
{
    unsigned int bus, slot, fn, v, bar, i;

    for (bus = 0; bus < 8; bus++)
        for (slot = 0; slot < 32; slot++)
            for (fn = 0; fn < 8; fn++) {
                v = cfg_leggi(bus, slot, fn, 0x00);
                if ((v & 0xFFFF) != SIS_VENDITORE) continue;
                if (((v >> 16) & 0xFFFF) != SIS_6330) continue;

                for (i = 0; i < 6; i++) {
                    bar = cfg_leggi(bus, slot, fn, 0x10 + i * 4);
                    if (bar != 0xFFFFFFFFu && (bar & 1) && (bar & 0xFFFC))
                        return bar & 0xFFFC;
                }
                return 0;
            }
    return 0;
}

/* Dove sta la coppia indice/dato di ciascun banco, dentro la finestra
 * rilocata. La mappa e' quella di sisfb, la stessa che usa sonda.drv -ponte. */
static const unsigned char PARTE_OFF[6] = { 0, 0x04, 0x10, 0x12, 0x14, 0x16 };

/* Il ponte video: e' qui che sta la scalatura del pannello LCD, ed e' un
 * lavoro NON FINITO — per questo `-ponte` e' un interruttore e non il modo
 * normale di lavorare.
 *
 * ! UNDICI DELLE TRENTAQUATTRO SCRITTURE LA SCHEDA NON LE ACCETTA. Si e'
 * scritta la tabella, si e' riletto il ponte, e undici registri si rileggono
 * al valore di prima: sono di sola lettura, o protetti da uno sblocco che non
 * conosciamo. Il risultato e' una scalatura impostata a meta', e mezza
 * scalatura e' peggio di nessuna: lo schermo torna pieno e storto, in modi
 * che cambiano da una combinazione all'altra. Provato sul ferro tre volte,
 * tre risultati diversi e nessuno usabile.
 *
 * Senza ponte il testo sta in un angolo e si legge. Quella e' la rete di
 * sicurezza che DIREZIONE.md chiede, ed e' meglio averla storta e certa che
 * piena e a caso.
 *
 * ! SENZA QUESTO IL TESTO TORNA IN UN ANGOLO. I banchi VGA rimettono il
 * riquadro 80x25 giusto e leggibile — provato sul ferro il 10 settembre
 * 2026 — ma il pannello e' un 1024x768 a temporizzazione fissa, e a portarci
 * sopra il riquadro e' il ponte. Finche' resta impostato per 800x600, il
 * riquadro resta un riquadro e intorno c'e' quello che c'era prima.
 *
 * ! E NON SI SCRIVONO TUTTI E 640: si scrivono i trentacinque che la
 * sottrazione fra i due referti dice diversi. Cinque banchi da 128 indici
 * contengono anche contatori e registri di stato, e non abbiamo il documento
 * che dica quali. */
static void ponte_applica(const SisPonte *t, int n)
{
    unsigned int off;
    int i;

    if (!g_ponte) return;

    if (g_finestra == 0) {
        if (!g_prova)
            printf("sis: la finestra rilocata non c'e': ponte non toccato.\n");
        return;
    }

    for (i = 0; i < n; i++) {
        off = PARTE_OFF[t[i].parte];

        if (g_prova) {
            printf("  Part%u[%02x] = %02x\n",
                   t[i].parte, t[i].indice, t[i].valore);
            continue;
        }

        ioport_out(g_finestra + off,     t[i].indice);
        ioport_out(g_finestra + off + 1, t[i].valore);
    }
}

/* Scrive un banco. `da` e' il primo indice che interessa: 0 per tutto il
 * banco, il primo esteso per la sola meta' SiS.
 *
 * ! SR00 NON SI SCRIVE QUI. E' il registro di reset del sequenziatore, e lo
 * governa applica(): fermo mentre si programma, acceso alla fine. Lasciarlo
 * dentro la tabella lo faceva ripartire alla PRIMA voce del banco, cioe'
 * prima di ogni altra cosa — la protezione durava due out. */
static void banco(unsigned int idx, unsigned int dat, const SisReg *t, int n,
                  const char *nome, unsigned int da)
{
    int i;

    for (i = 0; i < n; i++) {
        if (t[i].indice < da) continue;
        if (idx == SEQ_IDX && t[i].indice == 0x00) continue;
        if (g_prova) {
            printf("  %s%02x = %02x\n", nome, t[i].indice, t[i].valore);
            continue;
        }
        ioport_out(idx, t[i].indice);
        ioport_out(dat, t[i].valore);
    }
}

/* Il banco dell'attributo ha il suo cerimoniale: un flip-flop da azzerare
 * leggendo lo stato, e l'indice che si scrive sulla STESSA porta del dato.
 *
 * ! IL BIT 5 DELL'INDICE ACCENDE LO SCHERMO, e va rimesso alla fine. Finche'
 * e' spento la tavolozza la comanda la CPU e il monitor non riceve niente:
 * dimenticarlo lascia uno schermo nero con i registri giusti, che e' il modo
 * piu' crudele di sbagliare. */
/* ! AR00..AR0F NON SI SCRIVONO, MAI. Sono la tavolozza dell'attributo, e nel
 * referto valgono sedici 04 in fila perche' sonda.drv non riesce a rileggerli
 * (il perche' e' in cima al file). Riscritti, dipingono lo schermo di rosso
 * pieno. In testo li rimette il kernel con i valori canonici; in grafica a 32
 * bit non li guarda nessuno, il colore e' nel pixel. */
#define AR_PRIMO_BUONO 0x10

static void banco_attributo(const SisReg *t, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (t[i].indice < AR_PRIMO_BUONO) continue;
        if (g_prova) {
            printf("  AR%02x = %02x\n", t[i].indice, t[i].valore);
            continue;
        }
        (void)ioport_in(STATO);
        ioport_out(AC_IDX, t[i].indice);
        ioport_out(AC_IDX, t[i].valore);
    }

    if (g_prova) return;
    (void)ioport_in(STATO);
    ioport_out(AC_IDX, 0x20);
}

/* Il confine fra «standard VGA» e «esteso SiS». Non e' una stima: il
 * sequenziatore standard arriva a SR04 (SR05 e' il lucchetto), il CRTC
 * standard a CR18. Da li' in su e' roba del costruttore, e solo di quella
 * questo file e' l'unico che sa qualcosa. */
#define SR_PRIMO_ESTESO  0x06
#define CR_PRIMO_ESTESO  0x19

static void applica(int solo_estesi, unsigned int misc,
                    const SisReg *sr, int n_sr, const SisReg *cr, int n_cr,
                    const SisReg *gr, int n_gr, const SisReg *ar, int n_ar)
{
    unsigned int da_sr = solo_estesi ? SR_PRIMO_ESTESO : 0;
    unsigned int da_cr = solo_estesi ? CR_PRIMO_ESTESO : 0;

    if (!g_prova) {
        /* ! IL LUCCHETTO PRIMA DI TUTTO. Senza SR05 = 0x86 le scritture ai
         * registri estesi non hanno effetto e non danno errore: si otterrebbe
         * una modalita' a meta', con i registri standard cambiati e quelli
         * SiS rimasti dov'erano. Su questa macchina il BIOS lo lascia gia'
         * aperto (SR05 si rilegge a1), ma non e' una cosa su cui contare. */
        ioport_out(SEQ_IDX, 0x05);
        ioport_out(SEQ_DAT, 0x86);

        /* ! IL SEQUENZIATORE SI FERMA MENTRE SI CAMBIA LA TEMPORIZZAZIONE.
         * Cambiare i registri del CRTC con la scansione accesa da' un'immagine
         * strappata e, su certe schede, un monitor che si spegne per
         * protezione. */
        ioport_out(SEQ_IDX, 0x00);
        ioport_out(SEQ_DAT, 0x01);

        /* Il Misc porta la scelta dell'orologio e la polarita' dei sincronismi:
         * e' standard VGA, e quando si rimette il testo lo scrive il kernel. */
        if (!solo_estesi)
            ioport_out(MISC_W, misc);
    } else if (!solo_estesi) {
        printf("  Misc = %02x\n", misc);
    }

    banco(SEQ_IDX, SEQ_DAT, sr, n_sr, "SR", da_sr);

    if (!g_prova && !solo_estesi) {
        /* ! IL LUCCHETTO DEL CRTC E' UN ALTRO, ed e' il bit 7 di CR11: finche'
         * e' acceso, le scritture ai primi otto registri non hanno effetto e
         * non danno errore. E' la stessa trappola documentata in
         * kernel/arch/x86/vga_modo3.c. */
        ioport_out(CRTC_IDX, 0x11);
        ioport_out(CRTC_DAT, (unsigned int)(ioport_in(CRTC_DAT) & 0x7F));
    }

    banco(CRTC_IDX, CRTC_DAT, cr, n_cr, "CR", da_cr);

    /* Il controllore grafico e quello dell'attributo sono standard VGA da capo
     * a fondo: nella meta' SiS non c'e' niente da scriverci. */
    if (!solo_estesi) {
        banco(GC_IDX, GC_DAT, gr, n_gr, "GR", 0);
        banco_attributo(ar, n_ar);
    }

    if (!g_prova) {
        /* Il sequenziatore riparte: da qui l'immagine c'e'. */
        ioport_out(SEQ_IDX, 0x00);
        ioport_out(SEQ_DAT, 0x03);
    }
}

/* -----------------------------------------------------------------------------
 * IL RITOCCO: dove il modo 3 canonico non e' il modo 3 di QUESTO BIOS
 *
 * ! IL CARATTERE E' LARGO OTTO, NON NOVE, e da questo viene tutto il resto.
 * Il modo 3 dell'IBM VGA e' 720x400: ottanta colonne per NOVE punti. Il modo
 * testo che il BIOS di questo portatile lascia acceso e' 640x400 — ottanta
 * colonne per OTTO punti — e la differenza sta in un bit, SR01 bit 0.
 *
 * Finche' non contava nessuno la differenza non si vedeva. Adesso conta il
 * ponte video: le sue tabelle di scalatura sono state lette con il pannello
 * che riceveva 640 punti per riga, e riceverne 720 gli fa sfilare l'immagine.
 * E' quello che si vedeva il 10 settembre 2026 — schermo pieno ma scostato,
 * con la prima riga tagliata e due mezze righe di frammenti in fondo.
 *
 * ! E AR13 VA CON LUI. Il registro di scorrimento fine dei punti vale 8 nei
 * modi a nove punti e 0 in quelli a otto: e' la stessa scelta, detta al
 * controllore dell'attributo. Cambiarne uno solo sposta l'immagine di mezzo
 * carattere.
 *
 * Gli altri tre sono cosmesi, e si mettono perche' costano due out: la forma
 * del cursore (CR0a/CR0b: il BIOS lo fa alto due righe di punti, il modo 3
 * canonico una) e il lucchetto del CRTC, che il BIOS richiude e il kernel
 * lascia aperto.
 *
 * ! SI SCRIVONO DOPO modo_testo(), NON PRIMA. E' il kernel a mettere il modo 3
 * canonico, quindi qualunque cosa gli si scriva prima la sovrascrive lui.
 * --------------------------------------------------------------------------- */
static const SisReg ritocco_sr[] = {
    { 0x01, 0x01 },   /* bit 0: carattere largo 8 punti, non 9 */
};
static const SisReg ritocco_cr[] = {
    { 0x0a, 0x0e },   /* cursore: prima riga di punti */
    { 0x0b, 0x0f },   /* cursore: ultima riga di punti */
    { 0x11, 0x8e },   /* bit 7: si richiude il lucchetto del CRTC */
};
static const SisReg ritocco_ar[] = {
    { 0x13, 0x00 },   /* scorrimento fine: 0 nei modi a otto punti */
};

static void ritocca(void)
{
    if (!g_ritocco) return;

    /* Il sequenziatore si ferma: SR01 cambia la larghezza del punto, cioe'
     * la temporizzazione, e cambiarla a scansione accesa da' un'immagine
     * strappata. Stessa ragione di applica(). */
    if (!g_prova) {
        ioport_out(SEQ_IDX, 0x00);
        ioport_out(SEQ_DAT, 0x01);
    }

    banco(SEQ_IDX, SEQ_DAT, ritocco_sr,
          (int)(sizeof(ritocco_sr) / sizeof(ritocco_sr[0])), "SR", 0);
    banco(CRTC_IDX, CRTC_DAT, ritocco_cr,
          (int)(sizeof(ritocco_cr) / sizeof(ritocco_cr[0])), "CR", 0);
    banco_attributo(ritocco_ar,
                    (int)(sizeof(ritocco_ar) / sizeof(ritocco_ar[0])));

    if (!g_prova) {
        ioport_out(SEQ_IDX, 0x00);
        ioport_out(SEQ_DAT, 0x03);
    }
}

/* La scheda e' quella per cui queste tabelle sono state lette? */
static int e_la_nostra(int dillo)
{
    unsigned int v;

    /* Il modo piu' corto per riconoscerla senza il servizio PCI: i registri
     * estesi rispondono 0xA1 quando sono sbloccati, e nessuna scheda che non
     * sia una SiS lo fa. */
    ioport_out(SEQ_IDX, 0x05);
    ioport_out(SEQ_DAT, 0x86);
    ioport_out(SEQ_IDX, 0x05);
    v = (unsigned int)ioport_in(SEQ_DAT);

    if (v == 0xA1) {
        if (dillo) printf("sis: SR05 risponde a1: e' una SiS sbloccata\n");
        return 1;
    }

    if (dillo)
        printf("sis: SR05 risponde %02x invece di a1: questa non e' una SiS\n", v);
    return 0;
}

/* =============================================================================
 * GLI SPECCHI: quattro registri del ponte che sono COPIE del CRTC
 *
 * ! QUESTA NON E' UNA TABELLA DA RIPRODURRE, E' UN'INVARIANTE DA MANTENERE, e
 * la differenza e' tutta la differenza fra riprodurre una fotografia e sapere
 * cosa si sta facendo.
 *
 * Confrontando i due referti `-ponte` — testo del BIOS e 800x600 del BIOS —
 * quattro registri di Part1 hanno lo STESSO valore di un registro del CRTC in
 * TUTTE E DUE le modalita':
 *
 *     Part1[04] = Part1[05] = CR01    fine dell'area attiva in orizzontale
 *     Part1[0e] = Part1[10] = CR12    fine dell'area attiva in verticale
 *
 * Sono la stessa cosa detta ai due stadi della scheda: quante colonne il CRTC
 * manda, e quante lo scaler del pannello ne aspetta. Il BIOS li tiene uguali
 * perche' devono esserlo.
 *
 * ! ED E' ESATTAMENTE QUELLO CHE CI MANCAVA. Il 10 settembre 2026, dopo un
 * ripristino, `-stato` sulla macchina vera ha detto: il CRTC produce 80
 * colonne, il ponte ne aspetta 100. Il pannello stava scalando un'immagine
 * larga diversa da com'era, e si vedeva.
 *
 * ! DEGLI ALTRI DICIASSETTE NON SAPPIAMO NIENTE, e non e' per pigrizia: sono
 * stati cercati. Non sono copie di nessun registro del CRTC, e non sono
 * rapporti di scalatura — il rapporto fra il loro valore in testo e in grafica
 * non e' ne' 800/640 ne' 600/400 ne' i loro inversi, che sono i quattro
 * rapporti che un ratio dovrebbe avere qualunque sia la dimensione del
 * pannello. Restano dentro `-ponte`, che e' un esperimento e resta spento.
 * ========================================================================== */
/* La larghezza del PANNELLO, in colonne meno una, come tutti i registri di
 * fine area attiva.
 *
 * ! QUESTA E' L'UNICA COSA DI QUESTO FILE CHE NON SI PUO' RICAVARE A MACCHINA
 * ACCESA, e va detto. Il pannello di un portatile ha una risoluzione fissa che
 * nessun registro dichiara: si legge nel referto del BIOS in modo testo, dove
 * Part1[4a] vale 7f — centoventotto colonne, cioe' 1024 punti — mentre la
 * sorgente ne manda 640. Su un altro portatile e' un altro numero, e sbagliarlo
 * da' un'immagine stirata o tagliata.
 *
 * ! E DICE ANCHE LA POLITICA DEL BIOS, che vale la pena copiare: in TESTO
 * stira 640 fino a 1024, in 800x600 lascia l'uscita a 800 e mette il quadro in
 * mezzo al pannello con i bordi neri. Sono due scelte diverse per due
 * modalita' diverse, e le facciamo uguali. */
#define PANNELLO_COLONNE 0x7f

/* Scrive un registro del ponte e lo RILEGGE. Rende 1 se ha attecchito.
 *
 * ! MEZZO PONTE E' SCRIVIBILE, E VA VERIFICATO OGNI VOLTA. Su 34 registri
 * provati l'11 settembre 2026, undici si rileggono al valore di prima: sono di
 * sola lettura, o protetti da uno sblocco che non conosciamo. Scrivere e
 * andare avanti come se fosse fatto e' il modo in cui questo driver ha passato
 * una settimana a spiegare uno schermo storto con ipotesi. */
static int ponte_metti(unsigned int indice, unsigned int valore)
{
    unsigned int p1 = PARTE_OFF[1];

    ioport_out(g_finestra + p1, indice);
    ioport_out(g_finestra + p1 + 1, valore);

    ioport_out(g_finestra + p1, indice);
    return (unsigned int)ioport_in(g_finestra + p1 + 1) == valore;
}

static void specchi(unsigned int cr01, unsigned int cr12, unsigned int uscita)
{
    int ok = 1;

    if (!g_specchi || g_finestra == 0) return;

    if (g_prova) {
        printf("  Part1[04] = Part1[05] = %02x   (come CR01)\n", cr01);
        printf("  Part1[0e] = Part1[10] = %02x   (come CR12)\n", cr12);
        printf("  Part1[4a] = Part1[4b] = %02x   (uscita verso il pannello)\n",
               uscita);
        return;
    }

    ok &= ponte_metti(0x04, cr01);
    ok &= ponte_metti(0x05, cr01);
    ok &= ponte_metti(0x0e, cr12);
    ok &= ponte_metti(0x10, cr12);
    ok &= ponte_metti(0x4a, uscita);
    ok &= ponte_metti(0x4b, uscita);

    dico("sis: ponte: sorgente %u colonne, uscita %u colonne%s\n",
           cr01 + 1, uscita + 1,
           ok ? "." : "  ! qualcuno NON ha attecchito (vedi -stato)");
}

/* =============================================================================
 * -stato: LEGGERE la scheda, non solo scriverci
 *
 * ! UN DRIVER CHE NON SA RILEGGERSI LAVORA ALLA CIECA, ed e' costato una
 * settimana. Fra il 3 e il 10 settembre 2026 «lo schermo e' scostato», «il
 * testo sta in un angolo», «si vedono cinque righe sovrapposte» sono state
 * tre descrizioni a parole di tre stati che i registri sapevano dire con un
 * numero. Questa funzione trasforma quello che si vede in quello che c'e'.
 *
 * Il calcolo e' quello standard VGA, e vale su qualunque scheda:
 *
 *   punti per carattere   SR01 bit 0:  acceso 8, spento 9
 *   larghezza             (CR01 + 1) * punti
 *   altezza               CR12, piu' il bit 8 da CR07 bit 1 e il bit 9 da
 *                         CR07 bit 6, piu' uno
 *   altezza carattere     CR09 bits 4:0, piu' uno
 *   testo o grafica       GR06 bit 0
 *
 * ! VERIFICATO SU SETTE REFERTI PRESI DAL FERRO prima di scriverlo qui: rende
 * 640x400 a 25 righe sui tre referti del testo del BIOS, 800x600 sui tre della
 * grafica, e 720x400 su quello preso dopo un nostro ripristino sbagliato — che
 * era esattamente il difetto, visto per la prima volta come numero invece che
 * come «e' scostato».
 *
 * ! E POI CHIEDE AL PONTE SE E' D'ACCORDO. Part1 registro 04 contiene la
 * larghezza della sorgente che lo scaler del pannello si aspetta, nella stessa
 * unita' di CR01: nei referti del BIOS i due valori sono UGUALI, in testo e in
 * grafica. Quando non lo sono, il pannello riceve un'immagine larga diversa da
 * come la sta scalando — ed e' l'immagine storta che si vedeva.
 * ========================================================================== */
static unsigned char cr_leggi(unsigned int i)
{
    ioport_out(CRTC_IDX, i);
    return (unsigned char)ioport_in(CRTC_DAT);
}

static unsigned char sr_leggi(unsigned int i)
{
    ioport_out(SEQ_IDX, i);
    return (unsigned char)ioport_in(SEQ_DAT);
}

static unsigned char gr_leggi(unsigned int i)
{
    ioport_out(GC_IDX, i);
    return (unsigned char)ioport_in(GC_DAT);
}

static void stato(void)
{
    VideoInfo    v;
    unsigned int sr01, gr06, cr01, cr07, cr09, cr12;
    unsigned int punti, largh, alt, hcar;
    int          grafica;

    /* Il lucchetto si apre per leggere gli estesi e si rimette com'era: una
     * lettura non deve lasciare la macchina diversa da come l'ha trovata. */
    {
        unsigned int sr05 = sr_leggi(0x05);

        ioport_out(SEQ_IDX, 0x05);
        ioport_out(SEQ_DAT, 0x86);

        sr01 = sr_leggi(0x01);
        gr06 = gr_leggi(0x06);
        cr01 = cr_leggi(0x01);
        cr07 = cr_leggi(0x07);
        cr09 = cr_leggi(0x09);
        cr12 = cr_leggi(0x12);

        ioport_out(SEQ_IDX, 0x05);
        ioport_out(SEQ_DAT, sr05);
    }

    punti   = (sr01 & 0x01) ? 8 : 9;
    grafica = (gr06 & 0x01) ? 1 : 0;
    largh   = (cr01 + 1) * punti;
    alt     = (cr12 | (((cr07 >> 1) & 1) << 8) | (((cr07 >> 6) & 1) << 9)) + 1;
    hcar    = (cr09 & 0x1F) + 1;

    dico("sis: modalita' %s, carattere largo %u punti\n",
           grafica ? "grafica" : "testo", punti);
    dico("     il CRTC produce %ux%u", largh, alt);
    if (!grafica && hcar)
        dico("  (%u colonne per %u righe, carattere alto %u)",
               cr01 + 1, alt / hcar, hcar);
    dico("\n");

    /* ! IL CONFRONTO CON IL PONTE E' LA RIGA CHE CONTA, e si fa su tutte e
     * due le dimensioni: una sola tornerebbe anche con l'altra sbagliata. */
    if (g_finestra != 0) {
        unsigned int p1 = PARTE_OFF[1], p04, p0e, p4a;

        ioport_out(g_finestra + p1, 0x04);
        p04 = (unsigned int)ioport_in(g_finestra + p1 + 1);
        ioport_out(g_finestra + p1, 0x0e);
        p0e = (unsigned int)ioport_in(g_finestra + p1 + 1);
        ioport_out(g_finestra + p1, 0x4a);
        p4a = (unsigned int)ioport_in(g_finestra + p1 + 1);

        dico("     il ponte aspetta %u colonne (Part1[04] = %02x, CR01 = %02x)"
               "  %s\n", p04 + 1, p04, cr01,
               (p04 == cr01) ? "d'accordo" : "! NON D'ACCORDO");
        dico("     e in verticale         (Part1[0e] = %02x, CR12 = %02x)"
               "  %s\n", p0e, cr12,
               (p0e == cr12) ? "d'accordo" : "! NON D'ACCORDO");

        if (p04 != cr01 || p0e != cr12)
            dico("     ! il pannello sta scalando un'immagine di misure\n"
                   "       diverse da quelle che il CRTC gli manda.\n");

        /* ! LA LARGHEZZA IN USCITA E' L'ALTRA META' DELLA STORIA. Sorgente e
         * uscita d'accordo fra loro vuol dire solo che lo scaler sa cosa
         * riceve: se poi manda al pannello meno colonne di quante il pannello
         * ne ha, l'immagine sta in mezzo con i bordi neri e tutto torna lo
         * stesso. */
        dico("     manda al pannello %u colonne (Part1[4a] = %02x)  %s\n",
               p4a + 1, p4a,
               (p4a == p04) ? "non stira: quadro centrato"
                            : "stira fino a li'");
    } else {
        dico("     ponte: finestra rilocata non raggiungibile\n");
    }

    if (video_info(&v) == 0) {
        if (v.fisico == 0)
            dico("     il kernel scrive in modo testo, a 0xB8000\n");
        else
            dico("     il kernel disegna a 0x%08x, %ux%u a %u bit, passo %u\n",
                   v.fisico, v.larghezza, v.altezza, v.bit, v.passo);
    }
}

/* =============================================================================
 * LA FIGURA DI PROVA: rende il giudizio dell'occhio una parola sola
 *
 * ! QUELLO CHE I REGISTRI NON POSSONO DIRE. La scheda sa dire quante colonne
 * produce e quante il ponte ne aspetta, e quando i due numeri coincidono ha
 * finito il suo lavoro. Se poi il pannello mostri tutto o solo un pezzo, i
 * registri non lo sanno: lo sa solo chi guarda.
 *
 * Percio' si disegnano venticinque righe numerate, con un marcatore all'inizio
 * e uno alla fine di ognuna. Chi guarda non deve ricopiare niente: deve dire
 * se vede la riga 01 e la riga 25, e se vede le lettere agli estremi. Sono tre
 * si o no invece di una schermata da trascrivere.
 *
 * Le colonne sono ottanta esatte: se lo schermo ne mostra meno, la lettera di
 * destra sparisce; se il quadro e' scostato, spariscono le righe in cima o in
 * fondo. Il difetto si legge da COSA MANCA.
 * ========================================================================== */
static void figura(int quale)
{
    char riga[81];
    int  r, c;

    for (r = 1; r <= 25; r++) {
        for (c = 0; c < 80; c++) riga[c] = '.';

        riga[0] = (char)('0' + (r / 10));
        riga[1] = (char)('0' + (r % 10));
        riga[2] = '[';
        riga[77] = ']';
        riga[78] = (char)('0' + (r / 10));
        riga[79] = (char)('0' + (r % 10));

        /* Nella riga di mezzo il numero della variante, grande e al centro:
         * e' l'unica cosa che va riferita, e deve leggersi anche di sfuggita. */
        if (r == 13) {
            const char *t = "  VARIANTE ";
            int i, k = 30;

            for (i = 0; t[i]; i++) riga[k++] = t[i];
            riga[k++] = (char)('0' + quale);
            riga[k++] = ' ';
            riga[k++] = ' ';
        }

        riga[80] = '\0';
        printf("%s\n", riga);
    }
}

/* =============================================================================
 * -prova: tutte le combinazioni in un avvio, e il referto lo scrive lui
 *
 * ! SI PUO' TORNARE AL TESTO PIU' VOLTE, NON ALLA GRAFICA. Quando il kernel
 * rimette il modo testo smette di credere al framebuffer e ricomincia a
 * scrivere a 0xB8000 (vedi vga_ripristina_testo): da li' in poi rimettere la
 * scheda in grafica darebbe uno schermo di caratteri disegnati come pixel,
 * cioe' illeggibile, e la prova finirebbe li'. Le varianti si provano dunque
 * tutte nella stessa direzione — dalla grafica al testo, e poi dal testo al
 * testo.
 *
 * ! E VA DETTO CHE NON SONO NELLE STESSE CONDIZIONI. La prima parte dalla
 * grafica, le altre partono dal testo che ha lasciato quella prima. Se una
 * variante si comportasse diversamente da sola, questo referto non lo
 * direbbe. E' il prezzo di non riavviare quattro volte, ed e' scritto nel
 * referto invece che taciuto.
 * ========================================================================== */
static void applica_testo(int con_ritocco, int con_specchi, int con_ponte);

static void prova_tutte(void)
{
    static const struct {
        const char *nome;
        int ritocco, specchi, ponte;
    } varianti[] = {
        { "il minimo: registri SiS + modo 3 del kernel",        0, 0, 0 },
        { "+ specchi: il ponte allineato al CRTC",              0, 1, 0 },
        { "+ ritocco: carattere a 8 punti, come il BIOS",       1, 1, 0 },
        { "+ ponte: anche i registri che non capiamo",          1, 1, 1 },
    };
    unsigned int n = sizeof(varianti) / sizeof(varianti[0]);
    unsigned int i;

    dico("=============================================================\n");
    dico("PROVA DI sis.drv - quattro varianti in un avvio\n");
    dico("=============================================================\n");
    dico("! LA PRIMA PARTE DALLA GRAFICA, LE ALTRE DAL TESTO che ha\n");
    dico("  lasciato quella prima: al modo grafico non si torna, perche'\n");
    dico("  il kernel da qui in poi scrive a 0xB8000. Se una variante si\n");
    dico("  comportasse diversamente da sola, questo referto non lo dice.\n");
    dico("\n");
    dico("! A SCHERMO compaiono 25 righe numerate con dei marcatori agli\n");
    dico("  estremi. Di ogni variante servono tre risposte: si vede la\n");
    dico("  riga 01? si vede la 25? si vedono le parentesi a destra?\n");
    dico("\n--- stato di partenza ---\n");
    stato();
    svuota();

    for (i = 0; i < n; i++) {
        dico("\n");
        dico("=============================================================\n");
        dico("VARIANTE %u: %s\n", i + 1, varianti[i].nome);
        dico("=============================================================\n");

        applica_testo(varianti[i].ritocco, varianti[i].specchi,
                      varianti[i].ponte);
        stato();

        /* ! SI SCRIVE PRIMA DI DISEGNARE, e prima della pausa. Se la variante
         * successiva bloccasse la macchina, quello che si e' gia' misurato
         * dev'essere gia' sul dischetto. */
        svuota();

        figura((int)i + 1);

        /* Dieci secondi per guardare. Non si aspetta un tasto: su una
         * macchina che si sta studiando la tastiera puo' non rispondere, e
         * un'attesa senza scadenza fermerebbe la prova a meta'. */
        usleep(10000000);
    }

    dico("\n");
    dico("=============================================================\n");
    dico("FINE. Lo schermo e' rimasto sulla variante %u.\n", n);
    dico("Di ognuna serve solo: riga 01 visibile? riga 25? le ']' a\n");
    dico("destra? E quale delle quattro si vedeva meglio.\n");
    dico("=============================================================\n");
    svuota();
}

/* La sequenza del ripristino, in un posto solo: la usano sia -testo sia la
 * batteria di -prova, e averla scritta due volte vorrebbe dire due sequenze
 * che divergono senza che nessuno se ne accorga. */
static void applica_testo(int con_ritocco, int con_specchi, int con_ponte)
{
    int r = g_ritocco, sp = g_specchi, po = g_ponte;

    g_ritocco = con_ritocco;
    g_specchi = con_specchi;
    g_ponte   = con_ponte;

    applica(1, MISC_TESTO,
            sr_testo, SR_TESTO_N, cr_testo, CR_TESTO_N,
            gr_testo, GR_TESTO_N, ar_testo, AR_TESTO_N);

    if (con_ponte) ponte_applica(ponte_testo, PONTE_TESTO_N);

    modo_testo();
    ritocca();
    specchi(cr_leggi(0x01), cr_leggi(0x12), PANNELLO_COLONNE);

    g_ritocco = r;
    g_specchi = sp;
    g_ponte   = po;
}

static void aiuto(void)
{
    printf("sis.drv - cambia modalita' senza il BIOS, su una SiS M760GX\n\n");
    printf("  sis.drv -testo   rimette la console di testo 80x25\n");
    printf("  sis.drv -800     rimette 800x600 a 32 bit\n");
    printf("  sis.drv -n       dice cosa scriverebbe, senza scrivere\n");
    printf("  sis.drv -stato   dice cosa sta facendo la scheda ADESSO\n");
    printf("  sis.drv -prova F prova QUATTRO varianti in un avvio e\n");
    printf("                   scrive tutto in F: e' il modo di non\n");
    printf("                   ricopiare numeri da uno schermo storto\n");
    printf("  sis.drv ... F    aggiunge F a qualunque comando\n");
    printf("  sis.drv -i       dice se la scheda e' quella giusta\n");
    printf("\n! LE DUE QUI SOTTO SONO ESPERIMENTI, e di suo non si fanno:\n");
    printf("  sis.drv -ponte     prova anche il resto della scalatura\n");
    printf("  sis.drv -senzaspecchi  non allinea il ponte al CRTC\n");
    printf("  sis.drv -ritocco   mette il modo 3 di QUESTO BIOS (8 punti)\n");
    printf("  Con -ponte lo schermo torna pieno ma storto: la scalatura si\n");
    printf("  imposta solo a meta' (undici registri non li accetta), e mezza\n");
    printf("  scalatura e' peggio di nessuna. Senza, il testo sta in un\n");
    printf("  angolo e si legge - che e' quello che serve.\n\n");
    printf("! A COSA SERVE: non a scegliere la risoluzione — quella la mette\n");
    printf("  Stage 2 all'accensione — ma a RIMETTERE IL TESTO quando il\n");
    printf("  server grafico e' morto e lo schermo e' congelato. E' il pezzo\n");
    printf("  che DIREZIONE.md chiede dal 12 agosto 2026.\n\n");
    printf("! -testo SCRIVE SOLO I REGISTRI SiS e poi chiama modo_testo():\n");
    printf("  i registri VGA standard, il carattere e la tavolozza li\n");
    printf("  rimette il kernel, che ce li ha giusti. Del referto serve\n");
    printf("  solo la meta' che il kernel non puo' sapere.\n\n");
    printf("! I VALORI SONO LETTI DA UNA MACCHINA VERA, non calcolati: la\n");
    printf("  differenza fra due referti di sonda.drv. Valgono per QUELLA\n");
    printf("  scheda e QUEL pannello.\n");
}

int main(int argc, char **argv)
{
    int testo = 0, grafica = 0, statoq = 0, provaq = 0, i;
    const char *perc = NULL;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            if (ioport_bind(VGA_BASE, VGA_PORTE) != 0) {
                printf("sis: ioport_bind rifiutata\n");
                return 1;
            }
            return e_la_nostra(1) ? 0 : 1;
        }
        /* `-test` e' l'abbreviazione che viene da digitare, e sbagliarla
         * costava un giro di riavvio: si accetta. */
        if (strcmp(argv[i], "-stato") == 0) { statoq = 1; continue; }
        if (strcmp(argv[i], "-prova") == 0) { provaq = 1; continue; }
        if (strcmp(argv[i], "-testo") == 0 ||
            strcmp(argv[i], "-test")  == 0) { testo = 1; continue; }
        if (strcmp(argv[i], "-800") == 0)   { grafica = 1; continue; }
        if (strcmp(argv[i], "-n") == 0)     { g_prova = 1; continue; }
        if (strcmp(argv[i], "-ponte") == 0) { g_ponte = 1; continue; }
        if (strcmp(argv[i], "-senzaponte") == 0) { g_ponte = 0; g_specchi = 0; continue; }
        if (strcmp(argv[i], "-senzaspecchi") == 0) { g_specchi = 0; continue; }
        if (strcmp(argv[i], "-ritocco") == 0) { g_ritocco = 1; continue; }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            aiuto();
            return 0;
        }
        if (argv[i][0] != '-' && perc == NULL) { perc = argv[i]; continue; }
        printf("sis: non conosco '%s'. Prova -h.\n", argv[i]);
        return 1;
    }

    if (!testo && !grafica && !statoq && !provaq) { aiuto(); return 1; }

    if (ioport_bind(VGA_BASE, VGA_PORTE) != 0) {
        printf("sis: ioport_bind rifiutata.\n");
        printf("     mi chiamo *.drv e giro da root? il varco e' quello.\n");
        return 1;
    }

    /* La finestra rilocata si trova solo passando dal PCI, e se il PCI non si
     * apre si va avanti lo stesso: senza ponte il ripristino e' incompleto,
     * ma non e' peggio di com'era. */
    /* ! LA FINESTRA SERVE ANCHE A -stato, che il ponte non lo scrive. La riga
     * che dice se lo scaler e' d'accordo con il CRTC e' la piu' utile del
     * rapporto, e senza la finestra non si puo' scrivere. */
    if (g_ponte || statoq || provaq || g_specchi) {
        if (ioport_bind(PCI_INDIRIZZO, 8) == 0)
            g_finestra = trova_finestra();
        if (g_finestra != 0 && ioport_bind(g_finestra, 128) != 0)
            g_finestra = 0;
        if (g_finestra == 0 && g_ponte)
            printf("sis: finestra rilocata non raggiungibile: niente ponte.\n");
        else if (g_finestra != 0 && !g_prova && g_ponte)
            dico("sis: finestra rilocata a 0x%04x.\n", g_finestra);
    }

    /* ! IL FILE SI APRE PRIMA DI TOCCARE IL VIDEO. Dopo, lo schermo puo' non
     * essere piu' leggibile, e un messaggio di errore che nessuno vede e' come
     * non averlo dato. */
    if (perc != NULL) {
        g_fd = open(perc, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (g_fd < 0) {
            printf("sis: non riesco a creare %s (%d)\n", perc, g_fd);
            printf("     il supporto e' scrivibile? dal CD non si scrive.\n");
            return 1;
        }
        printf("sis: scrivo anche su %s\n", perc);
    }

    if (provaq) {
        if (!e_la_nostra(1)) {
            printf("sis: non tocco niente: le tabelle sono di una SiS 6330.\n");
            if (g_fd >= 0) close(g_fd);
            return 1;
        }
        prova_tutte();
        if (g_fd >= 0) close(g_fd);
        return 0;
    }

    if (statoq) {
        /* ! -stato LEGGE E BASTA, e per questo non chiede se la scheda e' la
         * nostra: i registri che guarda sono standard VGA, e il confronto col
         * ponte si fa solo se la finestra c'e'. Su una scheda qualunque le
         * prime due righe sono comunque vere. */
        stato();
        if (!testo && !grafica) {
            if (g_fd >= 0) { svuota(); close(g_fd); }
            return 0;
        }
    }

    if (!g_prova && !e_la_nostra(1)) {
        printf("sis: non tocco niente. Le tabelle sono di una SiS 6330 e su\n");
        printf("     un'altra scheda scriverebbero registri che non esistono.\n");
        return 1;
    }

    if (testo) {
        if (!g_prova)
            dico("sis: rimetto il testo 80x25 (senza BIOS).\n");

        /* Prima la meta' che solo noi conosciamo: si spegne la modalita'
         * estesa della SiS. Finche' quei registri tengono la scheda in
         * lineare, i registri VGA standard li si puo' scrivere quanto si
         * vuole senza vedere un carattere. */
        applica(1, MISC_TESTO,
                sr_testo, SR_TESTO_N, cr_testo, CR_TESTO_N,
                gr_testo, GR_TESTO_N, ar_testo, AR_TESTO_N);
        ponte_applica(ponte_testo, PONTE_TESTO_N);

        /* Poi la meta' che il kernel fa meglio di noi: registri standard
         * canonici, carattere nel piano 2, tavolozza del DAC, e la console
         * che ricomincia a scrivere a 0xB8000. */
        if (!g_prova) {
            modo_testo();
            ritocca();

            /* ! I VALORI SI RILEGGONO DALLA SCHEDA, non si prendono dalla
             * tabella. Fra la tabella e adesso ci sono passati modo_testo() —
             * che mette il modo 3 canonico del kernel — e ritocca(). Quello
             * che il ponte deve sapere e' cosa il CRTC sta producendo IN
             * QUESTO MOMENTO, non cosa avevamo in mente di produrre. */
            specchi(cr_leggi(0x01), cr_leggi(0x12), PANNELLO_COLONNE);

            dico("sis: fatto. Se leggi questa riga, il testo e' tornato.\n");
        } else {
            printf("  (poi modo_testo(): registri standard, carattere, "
                   "tavolozza)\n");
            ritocca();
        }
    } else {
        /* ! DA TESTO A GRAFICA LO SCHERMO SMETTE DI LEGGERSI, e la console
         * del kernel continua a scrivere caratteri dove adesso ci sono pixel.
         * Si avvisa prima, perche' dopo non si legge piu' niente. */
        if (!g_prova) {
            printf("sis: passo a 800x600. Da qui lo schermo non si legge\n");
            printf("     piu': il comando per tornare indietro e'\n");
            printf("       sis.drv -testo\n");
            usleep(500000);
        }
        applica(0, MISC_GRAFICA,
                sr_grafica, SR_GRAFICA_N, cr_grafica, CR_GRAFICA_N,
                gr_grafica, GR_GRAFICA_N, ar_grafica, AR_GRAFICA_N);
        ponte_applica(ponte_grafica, PONTE_GRAFICA_N);
        /* In grafica il BIOS non stira: uscita uguale alla sorgente. */
        if (!g_prova) specchi(cr_leggi(0x01), cr_leggi(0x12),
                              cr_leggi(0x01));
    }

    if (g_prova) printf("\n(-n: non ho scritto niente)\n");

    if (g_fd >= 0) { svuota(); close(g_fd); }
    return 0;
}

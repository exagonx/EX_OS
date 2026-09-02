/* =============================================================================
 * kernel/arch/x86/kbdprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LA PROVA DELLA TASTIERA — dove si ferma, fra il tasto e il programma
 *
 * Fra un tasto premuto e una lettera sullo schermo ci sono sei anelli, e
 * quando non compare niente sono tutti sospettati insieme:
 *
 *     1. la tastiera manda un codice al controller 8042
 *     2. l'8042 lo mette nel proprio buffer di uscita
 *     3. l'8042 alza la linea IRQ1
 *     4. il PIC la lascia passare (se non e' mascherata)
 *     5. il kernel consegna la notifica a /dev/kbd.drv
 *     6. kbd.drv la traduce e la da' a chi legge
 *
 * Questa prova taglia la catena in due: legge l'8042 A TAPPETO, senza
 * interrupt, e insieme conta gli IRQ1 che arrivano. Da li' la risposta e' una
 * di tre, e sono tre guasti diversi con tre cure diverse:
 *
 *     nessun codice, nessun IRQ   la tastiera non parla con l'8042. Su una
 *                                 macchina moderna vuol dire quasi sempre una
 *                                 tastiera USB la cui emulazione legacy del
 *                                 BIOS ha smesso quando il sistema ha preso
 *                                 in mano il controller
 *     codici si', IRQ no          l'8042 riceve ma non interrompe: bit IRQ1
 *                                 spento nel byte di configurazione, o linea
 *                                 mascherata nel PIC
 *     codici e IRQ                l'hardware fa il suo dovere: il guasto sta
 *                                 piu' in alto, in kbd.drv o in chi legge
 *
 * -----------------------------------------------------------------------------
 * ! SI FA A TAPPETO E NON CON GLI INTERRUPT, e le due cose convivono
 *
 * Leggere il buffer dell'8042 in un ciclo non richiede di possedere l'IRQ1:
 * il codice sta nel buffer comunque, che l'interrupt arrivi o no. E' proprio
 * questa la ragione per cui la prova sa distinguere il caso 2 dal caso 1 —
 * un metodo che dipendesse dall'interrupt non potrebbe dire niente su un
 * interrupt che non arriva.
 *
 * ! MA SE C'E' kbd.drv, I CODICI SE LI PRENDE LUI. Due lettori dello stesso
 * buffer si rubano i byte a vicenda, e questa prova vedrebbe la meta' di
 * quello che c'e' — cioe' misurerebbe male proprio la cosa che deve misurare.
 * Percio' se l'IRQ1 risulta rivendicato da un driver, la prova lo dice e si
 * ferma: si esegue da dist/diagnostic.img, che i moduli non li carica.
 * ============================================================================= */

#include "kernel.h"
#include "isr.h"
#include "syscall.h"
#include "tty.h"
#include "ipc.h"

/* --- Le porte del controller 8042 ---------------------------------------- */
#define KBC_DATI    0x60
#define KBC_STATO   0x64    /* in lettura */
#define KBC_CMD     0x64    /* in scrittura */

#define ST_OUT_PIENO  0x01  /* c'e' un byte da leggere in 0x60 */
#define ST_IN_PIENO   0x02  /* il controller non e' pronto a ricevere */

extern volatile uint32_t g_ticks;

static int kb_pronto_a_scrivere(void)
{
    int guard;
    for (guard = 0; guard < 100000; guard++)
        if (!(port_inb(KBC_STATO) & ST_IN_PIENO)) return 1;
    return 0;
}

static int kb_leggi(uint8_t *b, uint32_t ms)
{
    uint32_t fine = g_ticks + (ms / 10) + 1;

    while (g_ticks < fine) {
        if (port_inb(KBC_STATO) & ST_OUT_PIENO) {
            *b = port_inb(KBC_DATI);
            return 0;
        }
    }
    return -1;
}

static int kb_comando(uint8_t c)
{
    if (!kb_pronto_a_scrivere()) return -1;
    port_outb(KBC_CMD, c);
    return 0;
}

static int kb_alla_tastiera(uint8_t c)
{
    if (!kb_pronto_a_scrivere()) return -1;
    port_outb(KBC_DATI, c);
    return 0;
}

/* =============================================================================
 * Il gestore temporaneo dell'IRQ1
 *
 * ! DEVE SVUOTARE LA PORTA, e la prima versione non lo faceva. Il
 * ragionamento sbagliato era: «se il byte lo legge il gestore, il ciclo a
 * tappeto non lo vede piu' e la prova conta zero codici». Vero, e irrilevante
 * di fronte a quello che succede davvero: l'8042 tiene IRQ1 ALTA finche' il
 * buffer di uscita e' pieno. Un gestore che non legge non abbassa niente,
 * l'interrupt riparte subito dopo l'iret, e la macchina passa tutto il tempo
 * dentro il gestore — il ciclo a tappeto non gira quasi mai e la prova
 * riferisce «zero codici, zero interrupt» su una tastiera che sta mandando
 * codici a raffica. E' esattamente cio' che ho misurato in QEMU: trentacinque
 * tasti battuti, zero visti.
 *
 * Le due misure si tengono lo stesso, e in modo piu' onesto: i byte presi dal
 * gestore contano come «arrivati CON interrupt», quelli che il ciclo a tappeto
 * trova da se' come «arrivati SENZA». La differenza fra i due numeri e' quella
 * che serve a capire dove si ferma la catena.
 * ========================================================================== */
static volatile uint32_t g_kb_irq    = 0;   /* interrupt arrivati */
static volatile uint32_t g_kb_con    = 0;   /* byte presi dal gestore */
static volatile uint8_t  g_kb_ultimo = 0;

static void kb_irq1(InterruptFrame *f)
{
    (void)f;
    g_kb_irq++;

    while (port_inb(KBC_STATO) & ST_OUT_PIENO) {
        g_kb_ultimo = port_inb(KBC_DATI);
        g_kb_con++;
    }
}

/* =============================================================================
 * La prova, passo per passo
 * ========================================================================== */
static void passo(int n, const char *cosa)
{
    kprintf("\n[PASSO %d] %s\n", n, cosa);
}

static void esito(int ok, const char *dettaglio)
{
    kprintf("          %s  %s\n", ok ? "OK   " : "NO   ", dettaglio);
}

static FdPasso     *g_out = 0;
static unsigned int g_max = 0;
static unsigned int g_n   = 0;

static void nota(unsigned int p, unsigned int codice, int es,
                 unsigned int a, unsigned int b)
{
    if (!g_out || g_n >= g_max) return;
    g_out[g_n].passo  = p;
    g_out[g_n].codice = codice;
    g_out[g_n].esito  = es;
    g_out[g_n].a      = a;
    g_out[g_n].b      = b;
    g_n++;
}

/* =============================================================================
 * kbd_stato — le quattro cose che si possono sapere SENZA toccare l'8042
 *
 * ! ESISTE PERCHE' LA PROVA VERA NON SI PUO' FARE CON kbd.drv CARICATO, e la
 * domanda piu' urgente si puo' rispondere lo stesso. La tastiera ha due strade
 * che si escludono a vicenda — il driver ring3 e il gestore dentro il TTY — e
 * quando nessun tasto fa niente la prima cosa da sapere e' su quale delle due
 * si e' finiti. Sono quattro letture di variabili del kernel piu' una del PIC:
 * nessun comando all'8042, quindi nessun byte rubato a nessuno.
 * ========================================================================== */
int kbd_stato(FdPasso *out, unsigned int max)
{
    int      src = tty_input_source();
    uint32_t pid = irq_proprietario(1);
    int      kpid = ipc_lookup("kbd");
    uint8_t  imr = port_inb(0x21);

    g_out = out;
    g_max = out ? max : 0;
    g_n   = 0;

    kprintf("\n=== COM'E' MESSA LA TASTIERA ADESSO ===\n\n");

    kprintf("  strada in uso      %s\n",
            (src == TTY_INPUT_IPC)      ? "driver ring3 /dev/kbd.drv (IPC)" :
            (src == TTY_INPUT_INTERNAL) ? "ripiego: gestore IRQ1 nel kernel" :
                                          "NESSUNA (il TTY non serve la tastiera)");
    kprintf("  servizio 'kbd'     %s", (kpid > 0) ? "registrato, PID " : "NON registrato");
    if (kpid > 0) kprintf("%d", kpid);
    kprintf("\n");
    kprintf("  IRQ1 rivendicato   %s", pid ? "si', PID " : "da nessuno");
    if (pid) kprintf("%u", pid);
    kprintf("\n");
    kprintf("  maschera del PIC   0x%02x  -> IRQ1 %s\n",
            imr, (imr & 0x02) ? "CHIUSA (nessun interrupt puo' arrivare)"
                              : "aperta");
    kprintf("  notifiche IRQ1     %u consegnate finora\n",
            irq_notifiche_di(1));
    kprintf("  IRQ12 (mouse)      %s, %u notifiche\n",
            irq_proprietario(12) ? "rivendicato" : "di nessuno",
            irq_notifiche_di(12));

    nota(1, KB_SORGENTE, (src == TTY_INPUT_NONE) ? -1 : 0,
         (unsigned int)src, (unsigned int)((kpid > 0) ? kpid : 0));
    nota(2, KB_PIC, (imr & 0x02) ? -1 : 0, imr, pid);

    kprintf("\n");
    if (src == TTY_INPUT_IPC && kpid <= 0) {
        kprintf("  ! IL TTY ASPETTA UN SERVIZIO CHE NON C'E'. Il driver e'\n");
        kprintf("    stato caricato ma non si e' registrato: guardare le\n");
        kprintf("    righe 'kbd:' dell'avvio, e' li' che ha detto perche'.\n");
    } else if (src == TTY_INPUT_INTERNAL) {
        kprintf("  ! SI E' RIPIEGATI SUL GESTORE INTERNO. Vuol dire che\n");
        kprintf("    /dev/kbd.drv non c'era o non ha registrato il servizio\n");
        kprintf("    in tempo. Da qui in poi il driver ring3 non ricevera'\n");
        kprintf("    nessuna notifica: le due strade si escludono.\n");
    } else if (imr & 0x02) {
        kprintf("  ! LA LINEA 1 E' CHIUSA NEL PIC: qualunque cosa faccia la\n");
        kprintf("    tastiera, nessuno viene avvisato.\n");
    } else {
        kprintf("  Da qui tutto sembra a posto: la strada e' scelta, il\n");
        kprintf("  servizio c'e', la linea e' aperta.\n");
    }

    /* =====================================================================
     * ! E ADESSO LA MISURA CHE SEPARA LE DUE IPOTESI RIMASTE. Tutto quello
     * qui sopra e' statico: dice com'e' configurata la macchina, non se i
     * tasti arrivano. Venti secondi con l'invito a premere, e poi il
     * contatore delle notifiche riletto:
     *
     *     e' salito     l'hardware interrompe e il kernel consegna. Il
     *                   guasto e' sopra: nel driver, nel TTY o nella shell
     *     e' fermo      nessun interrupt arriva. Il guasto e' sotto: 8042,
     *                   PIC, o la tastiera stessa
     *
     * Sono due strade opposte, e finora si somigliavano.
     * ===================================================================== */
    {
        uint32_t prima  = irq_notifiche_di(1);
        uint32_t prima12 = irq_notifiche_di(12);
        uint32_t fine   = g_ticks + 2000;       /* 20 secondi a 100 Hz */
        uint32_t dopo, dopo12;

        kprintf("\n  PREMI DEI TASTI PER VENTI SECONDI, adesso.\n");
        kprintf("  (non si vedra' niente a schermo: si sta contando quante\n");
        kprintf("   volte il kernel avvisa il driver, non cosa si scrive)\n");

        while (g_ticks < fine) { }

        dopo   = irq_notifiche_di(1);
        dopo12 = irq_notifiche_di(12);
        kprintf("\n  notifiche IRQ1     %u -> %u  (%u in venti secondi)\n",
                prima, dopo, dopo - prima);
        kprintf("  notifiche IRQ12    %u -> %u  (%u in venti secondi)\n",
                prima12, dopo12, dopo12 - prima12);

        nota(3, KB_TASTI, (dopo > prima) ? 0 : -1, dopo - prima,
             dopo12 - prima12);

        /* ! IL MOUSE SI GUARDA INSIEME ALLA TASTIERA, e non per curiosita':
         * le due porte condividono il buffer di uscita del controller. Un
         * mouse che manda a raffica mentre la tastiera tace e' un indizio
         * preciso — non «due periferiche», ma una che occupa il posto
         * dell'altra. */
        if (dopo12 > prima12 && dopo == prima) {
            kprintf("\n  ! IL MOUSE INTERROMPE E LA TASTIERA NO, sulla stessa\n");
            kprintf("    porta e con lo stesso buffer. Il controller riceve\n");
            kprintf("    e consegna: e' la meta' tastiera a non arrivare.\n");
        }

        if (dopo > prima) {
            kprintf("\n  ! GLI INTERRUPT ARRIVANO E IL KERNEL LI CONSEGNA.\n");
            kprintf("    Se i tasti non compaiono lo stesso, il guasto e'\n");
            kprintf("    SOPRA questa riga: il driver li riceve e non li\n");
            kprintf("    traduce, o non li consegna a chi legge.\n");
        } else {
            kprintf("\n  ! NESSUN INTERRUPT IN VENTI SECONDI.\n");
            kprintf("    Il guasto e' SOTTO: l'8042 non alza IRQ1, oppure\n");
            kprintf("    qualcosa tiene pieno il suo buffer di uscita.\n");
            kprintf("    La prova completa da dist/diagnostic.img dice quale\n");
            kprintf("    delle due.\n");
        }
    }

    return (int)g_n;
}

int kbd_diagnostica(FdPasso *out, unsigned int max)
{
    uint8_t st, b, cfg = 0, imr_prima;
    uint32_t codici = 0, irq_prima;
    int r, guasti = 0;

    isr_handler_fn vecchio;

    g_out = out;
    g_max = out ? max : 0;
    g_n   = 0;

    kprintf("\n=== PROVA DELLA TASTIERA, PASSO PASSO ===\n");

    /* ! LA LINEA 1 SI PRENDE SUBITO, PER TUTTA LA PROVA, E NON SOLO PER IL
     * PASSO 8. Se /dev/kbd.drv non e' caricato, la tastiera la serve il TTY
     * dentro il kernel — e il suo gestore di IRQ1 SVUOTA la porta 0x60 a ogni
     * interrupt. Le risposte dell'8042 alle domande dei passi 2-6 finirebbero
     * li' dentro, e la prova direbbe «il controller non risponde» su una
     * macchina il cui controller risponde benissimo. Ci sono cascato: in QEMU
     * i passi da 2 a 6 fallivano tutti, e il colpevole era il lettore
     * legittimo dall'altra parte.
     *
     * ! E ALLA FINE SI RIMETTE QUELLO DI PRIMA, non NULL: registrare NULL
     * lascerebbe la console senza tastiera dopo la prova, cioe' la prova
     * romperebbe proprio la cosa che e' venuta a misurare. */
    vecchio = irq_handler_get(1);
    irq_register_handler(1, kb_irq1);

    /* ! E LA LINEA SI CHIUDE PER I PASSI DA 1 A 7, che sono tutti domande
     * all'8042 con risposta nel buffer di uscita. Su un controller VERO anche
     * la risposta a un comando alza IRQ1: con la linea aperta, il gestore si
     * prenderebbe il 0x55 dell'autodiagnosi prima che il ciclo a tappeto lo
     * veda, e la prova direbbe «il controller non risponde». In QEMU non si
     * vedeva — le risposte ai comandi del controller non alzano la linea —
     * ed e' il tipo di differenza per cui questa prova esiste. La linea si
     * riapre al passo 8, che e' l'unico che la vuole. */
    imr_prima = port_inb(0x21);
    pic_mask_irq(1);

    /* --- 0: c'e' un driver di mezzo? ---------------------------------- */
    if (irq_proprietario(1) != 0) {
        kprintf("\n! /dev/kbd.drv ha rivendicato l'IRQ1: i codici se li prende\n");
        kprintf("  lui e questa prova ne vedrebbe la meta'. Si esegue da\n");
        kprintf("  dist/diagnostic.img, che i moduli non li carica.\n");
        nota(0, KB_OCCUPATO, -1, 0, 0);
        irq_register_handler(1, vecchio);
        return (int)g_n;
    }

    /* --- 1 ------------------------------------------------------------ */
    passo(1, "Il controller 8042 c'e'? (lettura del registro di stato)");
    st = port_inb(KBC_STATO);
    kprintf("          stato=0x%02x  (0xFF vorrebbe dire: nessun controller)\n", st);
    nota(1, KB_STATO, (st == 0xFF) ? -1 : 0, st, 0);
    if (st == 0xFF) {
        esito(0, "nessun 8042: la tastiera di questa macchina non e' PS/2");
        kprintf("          Se e' USB, serve lo stack USB: vedi kernel.txt,\n");
        kprintf("          [modules] con pci e uhci.\n");
        irq_register_handler(1, vecchio);
        return (int)g_n;
    }
    esito(1, "il controller risponde");

    /* --- 2 ------------------------------------------------------------ */
    passo(2, "Autodiagnosi del controller (comando 0xAA, attesa 0x55)");
    kb_comando(0xAA);
    r = kb_leggi(&b, 500);
    if (r != 0) {
        esito(0, "nessuna risposta all'autodiagnosi");
        nota(2, KB_SELFTEST, -1, 0xFF, 0);
        guasti++;
    } else {
        kprintf("          risposta 0x%02x\n", b);
        esito(b == 0x55, b == 0x55 ? "l'8042 sta bene"
                                   : "l'8042 risponde ma non con 0x55");
        nota(2, KB_SELFTEST, (b == 0x55) ? 0 : -1, b, 0);
        if (b != 0x55) guasti++;
    }

    /* --- 3 ------------------------------------------------------------ */
    passo(3, "Prova della prima porta (comando 0xAB, attesa 0x00)");
    kb_comando(0xAB);
    r = kb_leggi(&b, 500);
    if (r != 0) {
        esito(0, "nessuna risposta");
        nota(3, KB_PORTA, -1, 0xFF, 0);
        guasti++;
    } else {
        kprintf("          risposta 0x%02x  (0x00 = porta a posto)\n", b);
        esito(b == 0x00, b == 0x00 ? "la porta della tastiera e' sana"
                                   : "la porta segnala un guasto elettrico");
        nota(3, KB_PORTA, (b == 0x00) ? 0 : -1, b, 0);
        if (b != 0x00) guasti++;
    }

    /* --- 4 ------------------------------------------------------------ */
    passo(4, "Byte di configurazione (comando 0x20): l'IRQ1 e' acceso?");
    kb_comando(0x20);
    if (kb_leggi(&cfg, 500) != 0) {
        esito(0, "il byte di configurazione non si legge");
        nota(4, KB_CONFIG, -1, 0, 0);
        guasti++;
    } else {
        kprintf("          config=0x%02x  bit0 (IRQ1)=%d  bit4 (clock spento)=%d"
                "  bit6 (traduzione)=%d\n",
                cfg, (cfg & 0x01) ? 1 : 0, (cfg & 0x10) ? 1 : 0,
                (cfg & 0x40) ? 1 : 0);

        /* ! LA TRADUZIONE E' IL TERZO BIT DA GUARDARE, e dimenticarlo costa un
         * guasto che sembra tutt'altro. Senza il bit 6 il controller consegna
         * gli scancode del SET 2 invece che del set 1: le tabelle del sistema
         * sono per il set 1, quindi le lettere escono — sbagliate, ma escono,
         * perche' qualche codice del set 2 cade su una lettera del set 1 — e
         * INVIO e BACKSPACE non escono affatto, perche' i loro codici (0x5A e
         * 0x66) nel set 1 non vogliono dire niente.
         *
         * Il sintomo e' «scrivo ma non posso andare a capo», che non
         * assomiglia per niente a «manca un bit nel byte di configurazione».
         * L'ho lasciato fuori dalla correzione qui sotto e l'ho pagato subito:
         * dopo la prova la shell accettava le lettere e non l'Invio. */
        if (!(cfg & 0x01) || (cfg & 0x10) || !(cfg & 0x40)) {
            esito(0, "configurazione incompleta: la sistemo");
            if (!(cfg & 0x01)) kprintf("          - IRQ1 era spento\n");
            if (cfg & 0x10)    kprintf("          - il clock della porta era spento\n");
            if (!(cfg & 0x40)) kprintf("          - la traduzione al set 1 era spenta\n");

            cfg = (uint8_t)((cfg | 0x01 | 0x40) & ~0x10);
            kb_comando(0x60);
            kb_alla_tastiera(cfg);
            kprintf("          ora config=0x%02x\n", cfg);
            nota(4, KB_CONFIG, -1, cfg, 0);
            guasti++;
        } else {
            esito(1, "IRQ1, clock e traduzione al set 1: tutti a posto");
            nota(4, KB_CONFIG, 0, cfg, 0);
        }
        /* La porta va comunque riabilitata: 0xAB/0xAA possono averla spenta. */
        kb_comando(0xAE);
    }

    /* --- 5 ------------------------------------------------------------ */
    passo(5, "Reset della tastiera (0xFF): attesi 0xFA e poi 0xAA");
    kb_alla_tastiera(0xFF);
    r = kb_leggi(&b, 1000);
    if (r == 0 && b == 0xFA) {
        uint8_t bat;
        kprintf("          ACK ricevuto (0xFA)\n");
        if (kb_leggi(&bat, 3000) == 0) {
            kprintf("          autodiagnosi della tastiera: 0x%02x\n", bat);
            esito(bat == 0xAA, bat == 0xAA ? "la tastiera si e' presentata"
                                           : "la tastiera risponde male");
            nota(5, KB_RESET, (bat == 0xAA) ? 0 : -1, 0xFA, bat);
            if (bat != 0xAA) guasti++;
        } else {
            esito(0, "ACK si', ma nessuna autodiagnosi dopo");
            nota(5, KB_RESET, -1, 0xFA, 0xFF);
            guasti++;
        }
    } else {
        kprintf("          risposta 0x%02x (r=%d)\n", (r == 0) ? b : 0xFF, r);
        esito(0, "la tastiera non risponde al reset: non c'e', o non e' PS/2");
        nota(5, KB_RESET, -1, (r == 0) ? b : 0xFF, 0);
        guasti++;
    }

    /* --- 6 ------------------------------------------------------------ */
    passo(6, "Accensione della scansione (0xF4): atteso 0xFA");
    kb_alla_tastiera(0xF4);
    r = kb_leggi(&b, 1000);
    kprintf("          risposta 0x%02x\n", (r == 0) ? b : 0xFF);
    esito(r == 0 && b == 0xFA, (r == 0 && b == 0xFA)
              ? "la tastiera ha acceso la scansione"
              : "nessun ACK: la tastiera non mandera' codici");
    nota(6, KB_SCANSIONE, (r == 0 && b == 0xFA) ? 0 : -1, (r == 0) ? b : 0xFF, 0);
    if (!(r == 0 && b == 0xFA)) guasti++;

    /* --- 7 ------------------------------------------------------------ */
    passo(7, "La linea IRQ1 e' aperta nel PIC?");
    {
        /* ! QUELLA DI PRIMA, non quella di adesso: la linea l'ho chiusa io
         * all'inizio della prova, e leggerla ora vorrebbe dire misurare la
         * propria impronta. */
        uint8_t imr = imr_prima;
        kprintf("          maschera del PIC=0x%02x  bit1 (IRQ1)=%d\n",
                imr, (imr & 0x02) ? 1 : 0);
        if (imr & 0x02) {
            esito(0, "IRQ1 era MASCHERATA: nessun interrupt poteva arrivare");
            kprintf("          la apro io per il passo successivo\n");
            nota(7, KB_PIC, -1, imr, 0);
            guasti++;
        } else {
            esito(1, "la linea 1 e' aperta");
            nota(7, KB_PIC, 0, imr, 0);
        }
    }

    /* --- 8 ------------------------------------------------------------ */
    passo(8, "PREMI UN TASTO: aspetto fino a trenta secondi");
    kprintf("          Si contano i codici che arrivano e gli interrupt che\n");
    kprintf("          li accompagnano. Sono due cose diverse, e la\n");
    kprintf("          differenza fra le due dice dove si ferma.\n\n");

    g_kb_irq = 0;
    pic_unmask_irq(1);
    irq_prima = 0;

    /* ! SI ASPETTA IL PRIMO TASTO, NON UN TEMPO FISSO. La prima versione
     * contava per dieci secondi e basta: chi sta davanti alla macchina deve
     * finire di leggere l'invito, capire che tocca a lui e allungare la mano,
     * e dieci secondi se ne vanno prima. Il risultato era «zero codici» su una
     * tastiera che funziona — cioe' la prova accusava l'innocente. Adesso
     * aspetta fino a trenta secondi il PRIMO codice, e da li' conta per altri
     * tre: quello che serve e' sapere se arriva qualcosa, non quanto. */
    {
        uint32_t limite = g_ticks + 3000;      /* 30 secondi a 100 Hz */
        uint32_t fine   = 0;
        uint32_t senza  = 0;                   /* byte arrivati senza interrupt */
        uint32_t visti  = 0;

        g_kb_con = 0;
        pic_unmask_irq(1);                     /* adesso si', la linea serve */

        while (g_ticks < limite) {
            /* Un byte che il gestore non ha preso e' un byte arrivato SENZA
             * interrupt: e' la misura che distingue «l'8042 non riceve» da
             * «l'8042 riceve e non interrompe». */
            if (port_inb(KBC_STATO) & ST_OUT_PIENO) {
                b = port_inb(KBC_DATI);
                senza++;
            }

            if (g_kb_con + senza != visti) {
                visti = g_kb_con + senza;
                kprintf("          %u codici (%u con interrupt, %u senza), "
                        "%u interrupt\n", visti, g_kb_con, senza, g_kb_irq);
                if (fine == 0) fine = g_ticks + 300;
            }

            if (fine != 0 && g_ticks >= fine) break;
        }

        codici    = g_kb_con + senza;
        irq_prima = g_kb_irq;

        if (codici == 0)
            kprintf("          (trenta secondi senza un solo codice)\n");
    }

    irq_register_handler(1, vecchio);
    /* La linea torna com'era: se era chiusa prima, chiuderla di nuovo — questa
     * prova non deve lasciare la macchina diversa da come l'ha trovata. */
    if (imr_prima & 0x02) pic_mask_irq(1); else pic_unmask_irq(1);

    kprintf("\n          totale: %u codici, %u interrupt\n", codici, irq_prima);
    nota(8, KB_TASTI, (codici > 0) ? 0 : -1, codici, irq_prima);

    if (codici == 0 && irq_prima == 0) {
        esito(0, "niente di niente: la tastiera non parla con l'8042");
        kprintf("          Se e' una tastiera USB, l'emulazione del BIOS ha\n");
        kprintf("          smesso: serve lo stack USB (pci + uhci in\n");
        kprintf("          [modules], vedi /boot/kernel.txt).\n");
        kprintf("          Se e' PS/2, provare un'altra tastiera: il passo 5\n");
        kprintf("          dice se ha risposto al reset.\n");
        guasti++;
    } else if (irq_prima == 0) {
        esito(0, "i codici arrivano ma l'IRQ1 no: e' il PIC o l'8042");
        kprintf("          I tasti si leggono a tappeto ma nessun interrupt\n");
        kprintf("          li accompagna: kbd.drv, che aspetta l'interrupt,\n");
        kprintf("          non vedrebbe mai niente.\n");
        guasti++;
    } else if (codici == 0) {
        esito(0, "interrupt senza codici: qualcuno legge il buffer prima");
        guasti++;
    } else {
        esito(1, "codici E interrupt: l'hardware fa il suo dovere");
        kprintf("          Il guasto e' piu' in alto: kbd.drv o chi legge.\n");
    }

    /* --- 9 ------------------------------------------------------------ */
    passo(9, "Rimetto il controller come serve a chi usa la macchina");
    kprintf("          L'autodiagnosi del passo 2 azzera il byte di\n");
    kprintf("          configurazione su un 8042 vero: se la prova finisse\n");
    kprintf("          qui, lascerebbe la tastiera peggio di come l'ha\n");
    kprintf("          trovata. Una diagnostica non deve rompere cio' che\n");
    kprintf("          e' venuta a misurare.\n");
    {
        uint8_t finale = 0;

        kb_comando(0x20);
        if (kb_leggi(&finale, 500) != 0) finale = 0x45;

        finale = (uint8_t)((finale | 0x01 | 0x40) & ~0x10);
        kb_comando(0x60);
        kb_alla_tastiera(finale);
        kb_comando(0xAE);                   /* porta della tastiera accesa */
        kb_alla_tastiera(0xF4);             /* e scansione accesa */
        kb_leggi(&b, 500);

        kprintf("          config finale=0x%02x (IRQ1, clock, traduzione)\n",
                finale);
        esito((finale & 0x41) == 0x41, ((finale & 0x41) == 0x41)
                  ? "la tastiera resta utilizzabile dopo la prova"
                  : "non sono riuscito a rimettere la configurazione");
        nota(9, KB_CONFIG, ((finale & 0x41) == 0x41) ? 0 : -1, finale, 1);
        if ((finale & 0x41) != 0x41) guasti++;
    }

    kprintf("\n=== %d passi con problemi ===\n", guasti);
    return (int)g_n;
}

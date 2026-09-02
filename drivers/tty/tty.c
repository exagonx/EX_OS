/* =============================================================================
 * drivers/tty/tty.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "vga.h"
#include "idt.h"
#include "isr.h"
#include "sched.h"
#include "ipc.h"
#include "tty.h"
#include "kbd_proto.h"
#include "syscall.h"   /* EINTR: una lettura interrotta lo rende */

/* =============================================================================
 * Due sorgenti di input, una sola interfaccia
 *
 * Da luglio 2026 la tastiera è servita da /dev/kbd.drv, un processo
 * ring3 che possiede IRQ1 e le porte del KBC e consegna righe complete
 * via IPC (vedi drivers/kbd/kbd.c). Il TTY resta nel kernel — è lui a
 * possedere la VGA e a implementare drv_write — ma per l'INPUT diventa
 * un semplice client di quel servizio.
 *
 * Tutto il codice storico (tabelle scancode, handler IRQ1, line
 * discipline, ring buffer) è conservato sotto TTY_INPUT_INTERNAL come
 * fallback: se kbd.drv manca dal floppy o il suo processo muore, il
 * kernel si riprende la tastiera invece di lasciare il sistema senza
 * console. Non è codice morto: è la sola strada percorribile finché il
 * driver non è caricato, ed è anche il riferimento di comportamento con
 * cui confrontare il driver ring3 quando qualcosa non torna.
 *
 * Chi legge, e in quale contesto:
 *   - modalità INTERNAL: l'handler IRQ1 gira in contesto interrupt e
 *     riempie g_input_buf; drv_read() lo consuma in contesto processo.
 *     Da qui tutte le precauzioni (volatile, cli attorno al blocco).
 *   - modalità IPC: nessun contesto interrupt è coinvolto. drv_read()
 *     gira nel contesto del processo che ha chiamato read(0) (di norma
 *     la shell), quindi può tranquillamente usare ipc_send/ipc_recv, che
 *     operano proprio sulla mailbox di proc_get_current().
 * ============================================================================= */
static int      g_input_src = TTY_INPUT_NONE;

/* PID del servizio kbd, risolto pigramente al primo read e memorizzato.
 * Azzerato se il servizio sparisce, così il read successivo lo ricerca. */
static uint32_t g_kbd_pid   = 0;

/* =============================================================================
 * Ring buffer input tastiera (solo modalità TTY_INPUT_INTERNAL)
 * ============================================================================= */
/* Deve reggere la riga piu' lunga che la shell accetta (MAX_LINE): una
 * riga piu' lunga del ring viene consegnata a pezzi, e il comando arriva
 * troncato senza che niente lo segnali. */
#define TTY_BUF_SIZE    512

typedef struct {
    char     buf[TTY_BUF_SIZE];
    uint32_t head;          /* Indice prossima lettura */
    uint32_t tail;          /* Indice prossima scrittura */
    uint32_t count;         /* Caratteri presenti */
} RingBuf;

/* volatile: il buffer è scritto dall'handler IRQ1 e letto da drv_read in
 * contesto processo. Senza volatile il compilatore (-O2) può tenere
 * g_input_buf.count in un registro attraverso il loop di attesa e non
 * accorgersi mai dell'aggiornamento fatto dall'interrupt. */
static volatile RingBuf g_input_buf;

/* PID del processo in attesa di input (per unblock dopo IRQ1) */
static volatile uint32_t g_waiting_pid = 0;

/* =============================================================================
 * Line editing buffer (line discipline "cooked")
 *
 * I caratteri digitati NON vengono messi subito nel ring buffer che
 * drv_read consuma: vengono accumulati qui, cosi' Backspace puo' modificarli
 * prima che siano "consegnati" al processo in lettura. Solo quando viene
 * premuto Invio (o il buffer di riga e' pieno) la riga intera viene
 * copiata nel ring buffer in un colpo solo.
 *
 * Senza questo passaggio intermedio, drv_read consuma ogni carattere dal
 * ring buffer quasi istantaneamente (il processo non aspetta l'Invio per
 * leggere), quindi un Backspace successivo non trova piu' nulla da
 * rimuovere dal ring buffer: lo schermo viene "pulito" ma il carattere
 * resta nel buffer di riga del chiamante (es. la shell).
 * ============================================================================= */
#define TTY_LINE_MAX    256

static char     g_line_buf[TTY_LINE_MAX];
static uint32_t g_line_len = 0;

/* =============================================================================
 * Colonne davvero disegnate per la riga in corso, e quali caratteri le
 * occupano.
 *
 * ! NON COINCIDONO CON g_line_len, E CONFONDERLE MANGIA IL PROMPT. Vale
 * qui la stessa storia di drivers/kbd/kbd.c: un Backspace che cancella
 * "una colonna per ogni carattere nel buffer" cancella anche cio' che sta
 * PRIMA della riga — cioe' il prompt, che non e' nostro. Qui il difetto
 * arrivava dall'altro lato: si ecoava anche quando la riga era piena e il
 * carattere non veniva accumulato, e si ecoavano i caratteri di controllo
 * come glifi casuali della code page 437.
 * ============================================================================= */
static uint8_t  g_line_vis[TTY_LINE_MAX];
static uint32_t g_line_col = 0;

/* Questo carattere occupa esattamente una colonna? Unico punto in cui si
 * decide cosa va sullo schermo, cosi' il Backspace non puo' rispondere in
 * modo diverso. Il tab e' escluso: avanza fino alla prossima tabulazione,
 * un numero di colonne che dipende da dove comincia il prompt, e disegnare
 * qualcosa che non si sa disfare e' esattamente il difetto da chiudere. */
static int tty_eco_visibile(char c)
{
    uint8_t u = (uint8_t)c;

    return (u >= 32 && u < 127);
}

/* =============================================================================
 * Scancodes PS/2 → ASCII (tastiera US QWERTY, solo tasti principali)
 *
 * Layout: scancode → ASCII (non-shifted)
 * Scancode 0 = non mappato
 * ============================================================================= */
static const uint8_t scancode_to_ascii[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8',  /*  0- 9 */
    '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', /* 10-19 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,    /* 20-29 */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',  /* 30-39 */
    '\'','`', 0,  '\\','z', 'x', 'c', 'v', 'b', 'n',   /* 40-49 */
    'm', ',', '.', '/', 0,  '*', 0,  ' ', 0,   0,       /* 50-59 */
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,     /* 60-69 */
    0,   0,   0,   0,   0,   0,   '-', 0,   0,   0,     /* 70-79 */
    '+', 0,   0,   0,   0,   0,   0,   0,   0,   0,     /* 80-89 */
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,     /* 90-99 */
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,     /* 100-109 */
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,     /* 110-119 */
    0,   0,   0,   0,   0,   0,   0,   0               /* 120-127 */
};

/* Mappa shift */
static const uint8_t scancode_to_ascii_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b','\t','Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N',
    'M', '<', '>', '?', 0,  '*', 0,  ' ', 0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   '-', 0,   0,   0,
    '+', 0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0
};

/* Stato modificatori */
static uint8_t g_shift   = 0;
static uint8_t g_ctrl    = 0;
static uint8_t g_caps    = 0;
static uint8_t g_e0      = 0;    /* Prefisso scancode esteso 0xE0 */

/* =============================================================================
 * ring_buf_put — Inserisce un carattere nel ring buffer
 * ============================================================================= */
static void ring_buf_put(char c)
{
    if (g_input_buf.count >= TTY_BUF_SIZE) return;  /* Buffer pieno */
    g_input_buf.buf[g_input_buf.tail] = c;
    g_input_buf.tail = (g_input_buf.tail + 1) % TTY_BUF_SIZE;
    g_input_buf.count++;
}

/* =============================================================================
 * ring_buf_get — Estrae un carattere dal ring buffer
 * Ritorna 0 se vuoto
 * ============================================================================= */
static char ring_buf_get(void)
{
    char c;
    if (g_input_buf.count == 0) return 0;
    c = g_input_buf.buf[g_input_buf.head];
    g_input_buf.head = (g_input_buf.head + 1) % TTY_BUF_SIZE;
    g_input_buf.count--;
    return c;
}

/* =============================================================================
 * kbd_irq1_handler — Handler IRQ1 (tastiera PS/2)
 *
 * Legge lo scancode dalla porta 0x60, lo traduce in ASCII,
 * lo inserisce nel ring buffer e sblocca eventuali processi in attesa.
 * ============================================================================= */
static void kbd_irq1_handler(InterruptFrame *frame)
{
    uint8_t scancode;
    char    ascii = 0;

    (void)frame;

    /* Leggi scancode dalla porta dati KBC */
    scancode = port_inb(0x60);

    /* Gestisci prefisso esteso E0 (tasti speciali: frecce, ins, del, ecc.) */
    if (scancode == 0xE0) {
        g_e0 = 1;
        return;
    }

    /* Key release: bit 7 = 1 */
    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;
        /* Aggiorna stato modificatori al rilascio */
        if (key == 0x2A || key == 0x36) g_shift = 0;  /* SHIFT */
        if (key == 0x1D) g_ctrl = 0;                   /* CTRL */
        g_e0 = 0;
        return;
    }

    /* Key press */
    if (g_e0) {
        /* Tasti estesi: frecce direzionali, insert, delete, home, end, pgup, pgdn */
        switch (scancode) {
            case 0x48: ascii = '\x1B'; ring_buf_put(ascii);  /* Up → ESC[A */
                       ring_buf_put('['); ring_buf_put('A'); g_e0=0; return;
            case 0x50: ascii = '\x1B'; ring_buf_put(ascii);  /* Down → ESC[B */
                       ring_buf_put('['); ring_buf_put('B'); g_e0=0; return;
            case 0x4D: ascii = '\x1B'; ring_buf_put(ascii);  /* Right → ESC[C */
                       ring_buf_put('['); ring_buf_put('C'); g_e0=0; return;
            case 0x4B: ascii = '\x1B'; ring_buf_put(ascii);  /* Left → ESC[D */
                       ring_buf_put('['); ring_buf_put('D'); g_e0=0; return;
            case 0x53: ring_buf_put(127); g_e0=0; return;   /* Delete → DEL */
        }
        g_e0 = 0;
        return;
    }

    /* Aggiorna stato modificatori */
    if (scancode == 0x2A || scancode == 0x36) { g_shift = 1; return; }
    if (scancode == 0x1D)                      { g_ctrl  = 1; return; }
    if (scancode == 0x3A) { g_caps ^= 1; return; }  /* CAPS LOCK toggle */

    /* Traduzione scancode → ASCII */
    if (scancode < 128) {
        if (g_shift) {
            ascii = (char)scancode_to_ascii_shift[scancode];
        } else {
            ascii = (char)scancode_to_ascii[scancode];
        }

        /* CapsLock: inverte maiuscole/minuscole per lettere */
        if (g_caps && ascii >= 'a' && ascii <= 'z') ascii -= 32;
        if (g_caps && ascii >= 'A' && ascii <= 'Z') ascii += 32;

        /* Ctrl + lettera: genera codice controllo (es. Ctrl+C = 3) */
        if (g_ctrl && ascii >= 'a' && ascii <= 'z') {
            ascii = (char)(ascii - 'a' + 1);
        }
    }

    if (ascii == 0) return;

    /* Line discipline (cooked): i caratteri vengono accumulati in
     * g_line_buf e NON nel ring buffer, cosi' restano modificabili
     * (Backspace) finche' la riga non e' confermata con Invio. */

    if (ascii == '\b') {
        /* Backspace: rimuovi ultimo carattere dal buffer di riga e dallo
         * schermo. Qui il carattere e' garantito presente (non e' ancora
         * stato consegnato a drv_read), quindi la cancellazione funziona
         * sempre, indipendentemente da quanto e' rapida la lettura. */
        if (g_line_len > 0) {
            if (g_line_vis[--g_line_len]) {
                g_line_col--;
                vga_putchar('\b');
                vga_putchar(' ');
                vga_putchar('\b');
            }
        }

        /* Arrivati al limite la riga si azzera del tutto: cio' che resta e'
         * invisibile, non cancellabile a vista, e finirebbe dentro il
         * comando che si sta per battere. Chi cancella fino in fondo deve
         * trovare una riga vuota davvero. */
        if (g_line_col == 0) g_line_len = 0;
        return;
    }

    if (ascii == '\n' || ascii == '\r') {
        vga_putchar('\n');
        /* Riga confermata: copia tutto il buffer di riga nel ring buffer
         * in un solo colpo, poi il terminatore di riga. */
        uint32_t i;
        for (i = 0; i < g_line_len; i++) {
            ring_buf_put(g_line_buf[i]);
        }
        ring_buf_put('\n');
        g_line_len = 0;
        g_line_col = 0;

        /* Sblocca processo in attesa di input */
        if (g_waiting_pid != 0) {
            sched_unblock(g_waiting_pid);
            g_waiting_pid = 0;
        }
        return;
    }

    /* ! PRIMA SI DECIDE SE IL CARATTERE ENTRA, POI LO SI ECOA. L'ordine
     * inverso lascia sullo schermo caratteri che nel buffer non ci sono:
     * si esegue meno di quello che si legge. A riga piena si rifiuta. */
    if (g_line_len >= TTY_LINE_MAX - 1) return;

    g_line_vis[g_line_len]   = (uint8_t)tty_eco_visibile(ascii);
    g_line_buf[g_line_len++] = ascii;

    if (g_line_vis[g_line_len - 1]) {
        g_line_col++;
        vga_putchar(ascii);
    }
}

/* =============================================================================
 * drv_init — Inizializza il driver TTY
 *
 * NON registra più l'handler IRQ1: a questo punto del boot (PASSO 14)
 * non si sa ancora se /dev/kbd.drv verrà caricato, e i due percorsi si
 * escludono a vicenda — irq_handler() (isr.c) dà la precedenza a un
 * handler kernel registrato rispetto al proprietario ring3 dell'IRQ,
 * quindi un handler registrato qui "per sicurezza" impedirebbe al driver
 * ring3 di vedere un solo scancode. La scelta è rimandata a
 * tty_set_input_source(), chiamata da kernel_main al PASSO 14b quando
 * l'esito del caricamento è noto.
 * ============================================================================= */
int drv_init(void)
{
    klog(LOG_INFO, "TTY: inizializzazione driver...");

    /* Azzera ring buffer */
    g_input_buf.head  = 0;
    g_input_buf.tail  = 0;
    g_input_buf.count = 0;
    g_shift = g_ctrl = g_caps = g_e0 = 0;
    g_waiting_pid = 0;
    g_line_len = 0;
    g_line_col = 0;
    g_input_src = TTY_INPUT_NONE;
    g_kbd_pid   = 0;

    /* Svuota buffer tastiera KBC: qualunque byte lasciato lì dal BIOS
     * tiene alto OBF e impedisce al KBC di generare nuovi fronti su
     * IRQ1 — sia per l'handler interno sia per il driver ring3. */
    while (port_inb(0x64) & 0x01) {
        port_inb(0x60);
    }

    klog(LOG_INFO, "TTY: output VGA pronto, sorgente input da definire");
    klog(LOG_INFO, "TTY: ring buffer input %u byte", TTY_BUF_SIZE);

    return 0;
}

/* =============================================================================
 * tty_set_input_source — sceglie chi serve la tastiera
 *
 * Chiamata da kernel_main (PASSO 14b) con l'esito del caricamento di
 * /dev/kbd.drv, e da drv_read() stessa se il servizio kbd sparisce a
 * runtime. Idempotente: richiamarla con la sorgente già attiva non fa
 * nulla.
 * ============================================================================= */
/* =============================================================================
 * Per poll() — vedi il commento in drivers/tty/tty.h
 * ========================================================================== */
int tty_input_pronto_locked(void)
{
    /* ! SI GUARDA IL RING BUFFER, NON IL BUFFER DI RIGA. In modo «cooked» i
     * caratteri digitati restano nell'editor di riga finche' non si batte
     * Invio, e solo allora la riga finisce qui dentro. E' esattamente cio'
     * che deve vedere chi aspetta: una read che partisse a meta' riga si
     * bloccherebbe comunque, e poll() avrebbe mentito. */
    return g_input_buf.count != 0;
}

int tty_attesa_registra_locked(unsigned int pid)
{
    if (g_waiting_pid == 0 || g_waiting_pid == pid) {
        g_waiting_pid = pid;
        return 1;
    }
    return 0;                   /* di qualcun altro: chi chiama ripiega */
}

void tty_attesa_togli_locked(unsigned int pid)
{
    /* Solo il proprio: se la sveglia e' arrivata, l'handler ha gia' azzerato
     * il campo e potrebbe averlo riassegnato a un altro. */
    if (g_waiting_pid == pid) g_waiting_pid = 0;
}

int tty_input_source(void)
{
    return g_input_src;
}

void tty_set_input_source(int src)
{
    if (src == g_input_src) return;

    if (src == TTY_INPUT_INTERNAL) {
        /* Riprendi la tastiera in kernel space. Nota: non serve
         * disfare l'eventuale claim ring3 su IRQ1 — irq_handler() dà
         * comunque la precedenza all'handler registrato qui. */
        g_input_buf.head  = 0;
        g_input_buf.tail  = 0;
        g_input_buf.count = 0;
        g_line_len        = 0;
        g_line_col        = 0;
        g_waiting_pid     = 0;
        g_shift = g_ctrl = g_caps = g_e0 = 0;

        /* Il driver ring3 potrebbe essere morto con un byte non letto in
         * 0x60: finché OBF resta alto il KBC non produce altri fronti e
         * la tastiera sembrerebbe rotta anche dopo il fallback. */
        while (port_inb(0x64) & 0x01) {
            port_inb(0x60);
        }

        /* =================================================================
         * ! IL CONFIGURATION BYTE VA RIPROGRAMMATO ANCHE QUI, e non solo in
         * /dev/kbd.drv. E' il buco che la correzione del 31 luglio 2026 ha
         * lasciato aperto: quel giorno si e' scoperto che su un 8042 VERO il
         * comando di self-test 0xAA reinizializza il configuration byte, e
         * che la configurazione predefinita ha il bit 0 — «alza IRQ1» — a
         * ZERO. La cura e' stata scritta dentro kbd_hw_init(), cioe' dentro
         * il driver ring3, e questo ripiego non l'ha mai avuta.
         *
         * Conseguenza: se kbd.drv non si carica o non registra il servizio in
         * tempo — e su una macchina lenta che legge da floppy succede — il
         * sistema ripiega QUI, e qui la tastiera resta muta lo stesso, per un
         * motivo diverso da quello per cui il driver non e' partito. Due
         * guasti che sembrano uno: il prompt compare e nessun tasto fa niente.
         *
         * In emulazione non si vede, perche' l'8042 di QEMU non tocca il
         * proprio registro di modo quando esegue 0xAA.
         *
         * Read-modify-write e non un valore fisso: nel byte ci sono anche la
         * seconda porta PS/2 e il system flag, che non ci riguardano.
         * ================================================================= */
        {
            int guard, cfg = -1;

            for (guard = 0; guard < 100000; guard++)
                if (!(port_inb(0x64) & 0x02)) break;
            port_outb(0x64, 0x20);                  /* leggi configurazione */

            for (guard = 0; guard < 100000; guard++) {
                if (port_inb(0x64) & 0x01) { cfg = port_inb(0x60); break; }
            }

            if (cfg < 0) {
                /* Non si e' potuto leggere: si scrive un valore prudente —
                 * IRQ1 acceso, clock acceso, traduzione al set 1 — invece di
                 * lasciare il controller com'e'. Un byte inventato che
                 * accende la tastiera e' meglio di uno giusto che la lascia
                 * spenta. */
                cfg = 0x45;
                klog(LOG_WARN, "TTY: configuration byte non leggibile, "
                     "uso il ripiego 0x45");
            }

            cfg |=  0x01;       /* bit 0: IRQ1. Senza, la tastiera e' muta */
            cfg &= ~0x10;       /* bit 4 ALTO = clock della porta DISABILITATO */
            cfg |=  0x40;       /* bit 6: traduzione al set 1, come le tabelle */

            for (guard = 0; guard < 100000; guard++)
                if (!(port_inb(0x64) & 0x02)) break;
            port_outb(0x64, 0x60);                  /* scrivi configurazione */
            for (guard = 0; guard < 100000; guard++)
                if (!(port_inb(0x64) & 0x02)) break;
            port_outb(0x60, (uint8_t)cfg);

            /* ! E SI RILEGGE. Su un 8042 vero una scrittura sul byte di
             * configurazione non e' un fatto ma una richiesta: puo' non
             * attecchire, e allora la tastiera resta muta con tutto il
             * codice che dice di aver funzionato. Rileggere costa due
             * accessi a una porta e trasforma un guasto silenzioso in una
             * riga di log. */
            {
                int riletto = -1;

                for (guard = 0; guard < 100000; guard++)
                    if (!(port_inb(0x64) & 0x02)) break;
                port_outb(0x64, 0x20);
                for (guard = 0; guard < 100000; guard++)
                    if (port_inb(0x64) & 0x01) { riletto = port_inb(0x60); break; }

                if (riletto < 0 || !(riletto & 0x01) || (riletto & 0x10) ||
                    !(riletto & 0x40)) {
                    klog(LOG_ERROR, "TTY: l'8042 non tiene la configurazione "
                         "(voluto 0x%02x, riletto 0x%02x): tastiera in dubbio",
                         cfg, riletto);
                } else {
                    cfg = riletto;
                }
            }

            /* E la porta della tastiera accesa: 0xAA e 0xAB la spengono. */
            for (guard = 0; guard < 100000; guard++)
                if (!(port_inb(0x64) & 0x02)) break;
            port_outb(0x64, 0xAE);

            klog(LOG_INFO, "TTY: configuration byte dell'8042 = 0x%02x "
                 "(IRQ1 acceso)", cfg);
        }

        irq_register_handler(1, kbd_irq1_handler);
        pic_unmask_irq(1);
        g_input_src = TTY_INPUT_INTERNAL;
        klog(LOG_INFO, "TTY: input dalla tastiera gestito in-kernel (IRQ1 diretto)");
        return;
    }

    if (src == TTY_INPUT_IPC) {
        /* ! SI TOGLIE IL GESTORE KERNEL, e senza questo il ritorno all'IPC
         * non servirebbe a niente: irq_handler() (isr.c) da' la precedenza a
         * un handler registrato qui rispetto al proprietario ring3 della
         * linea. Lasciarlo su vorrebbe dire un TTY che crede di parlare col
         * driver mentre gli scancode se li prende ancora il kernel, e il
         * driver che aspetta per sempre una notifica che non arrivera'. */
        irq_register_handler(1, NULL);
        pic_unmask_irq(1);

        g_kbd_pid   = 0;
        g_input_src = TTY_INPUT_IPC;
        klog(LOG_INFO, "TTY: input dal servizio ring3 '%s' via IPC",
             KBD_SERVICE_NAME);
        return;
    }

    g_input_src = TTY_INPUT_NONE;
}

/* =============================================================================
 * tty_read_ipc — legge una riga dal servizio kbd (processo ring3)
 *
 * Gira nel contesto del processo che ha chiamato read(0): è la sua
 * mailbox che riceve la risposta, ed è lui che viene bloccato in attesa.
 * Il kernel qui non fa da intermediario di dati — si limita a prestare
 * il proprio codice al processo chiamante.
 *
 * Sequenza: ipc_send(KBD_MSG_READLINE) → ipc_recv() bloccante. Non c'è
 * finestra di "lost wakeup" come nel percorso interno: se il driver ha
 * già una riga pronta e risponde prima che il chiamante arrivi alla
 * recv, il messaggio resta nella mailbox e ipc_recv() lo trova subito.
 * ============================================================================= */
#define TTY_KBD_LOOKUP_TRIES    200   /* ~2s a 10ms per tentativo */

/* =============================================================================
 * tty_forse_torna_al_driver — il ripiego non e' una condanna
 *
 * ! FINO AL 2 SETTEMBRE 2026 IL RIPIEGO ERA DEFINITIVO. Una volta caduti sul
 * gestore interno non si tornava piu' indietro, per tutta la sessione: e
 * siccome ci si cadeva per una CORSA all'avvio — cinque shell, una cassetta
 * postale da quattro — bastava un attimo di sfortuna nei primi secondi per
 * lasciare kbd.drv affamato fino allo spegnimento.
 *
 * Un ripiego che si puo' disfare trasforma quella corsa in un inciampo: si
 * riprova ogni tanto, e appena il servizio risponde si torna al driver.
 *
 * ! NON SI RIPROVA A OGNI read(), che vorrebbe dire un ipc_lookup per ogni
 * tasto. Una volta ogni cento chiamate e' abbastanza spesso da recuperare in
 * un attimo e abbastanza raro da non pesare.
 * ============================================================================= */
static void tty_forse_torna_al_driver(void)
{
    static uint32_t quando = 0;
    int32_t pid;

    if (g_input_src != TTY_INPUT_INTERNAL) return;
    if (++quando < 100) return;
    quando = 0;

    pid = ipc_lookup(KBD_SERVICE_NAME);
    if (pid <= 0) return;

    klog(LOG_INFO, "TTY: il servizio '%s' risponde di nuovo (PID %d), "
         "torno al driver ring3", KBD_SERVICE_NAME, pid);
    tty_set_input_source(TTY_INPUT_IPC);
}

static int tty_read_ipc(char *dst, uint32_t n)
{
    IpcMessage meta;
    KbdReadLine richiesta;
    int32_t    r;

    if (proc_get_current() == NULL) {
        /* Nessun processo corrente: siamo nel kernel prima dello
         * scheduler. In modalità IPC non c'è nulla da fare — non
         * esiste una mailbox su cui ricevere. Non dovrebbe accadere:
         * la sorgente IPC è selezionata solo al PASSO 14b. */
        klog(LOG_ERROR, "TTY: read IPC fuori da un contesto processo");
        return -1;
    }

    /* Risoluzione pigra del PID del servizio. Al primo read la shell può
     * arrivare qui prima che il driver, appena reso READY al PASSO 14b,
     * abbia eseguito la propria ipc_register(): si riprova invece di
     * fallire subito. */
    if (g_kbd_pid == 0) {
        int tries;
        for (tries = 0; tries < TTY_KBD_LOOKUP_TRIES; tries++) {
            int32_t pid = ipc_lookup(KBD_SERVICE_NAME);
            if (pid > 0) {
                g_kbd_pid = (uint32_t)pid;
                break;
            }
            sched_sleep(10);
        }
        if (g_kbd_pid == 0) {
            klog(LOG_ERROR, "TTY: servizio '%s' non registrato - "
                 "ripiego sulla tastiera in-kernel", KBD_SERVICE_NAME);
            tty_set_input_source(TTY_INPUT_INTERNAL);
            return -1;
        }
    }

    richiesta.max = n;
    if (richiesta.max > KBD_LINE_MAX) richiesta.max = KBD_LINE_MAX;

    /* La console di chi ha chiamato read(0), non quella visibile: il
     * driver deve sapere a quale schermo appartiene questa richiesta,
     * perché serve solo chi sta sulla console in primo piano e mette
     * gli altri in attesa. Vedi drivers/kbd/kbd.c. */
    richiesta.console = proc_get_current()->console;

    /* =====================================================================
     * ! UNA CASSETTA PIENA NON E' UN DRIVER MORTO, e confonderle e' costato
     * la tastiera su hardware vero.
     *
     * Al PASSO 15 il kernel avvia UNA SHELL PER CONSOLE VIRTUALE — cinque —
     * e ognuna chiama read(0), cioe' manda una KBD_MSG_READLINE a kbd.drv.
     * La sua mailbox e' profonda QUATTRO (IPC_MAILBOX_DEPTH). La quinta
     * richiesta trova pieno, ipc_send ritenta e alla fine rende -EBUSY.
     *
     * Il codice di prima trattava qualunque valore negativo come «il driver
     * e' morto» e ripiegava sul gestore interno — per sempre, perche' quel
     * ripiego non tornava mai indietro. Da quel momento kbd.drv era affamato:
     * irq_handler() da' la precedenza all'handler kernel, e il driver
     * aspettava notifiche che non arrivavano piu'.
     *
     * ! E SI VEDEVA SOLO SU MACCHINA VERA, per una ragione che sembra assurda
     * e non lo e': con `verboseboot = 1` l'avvio stampa tanto, le cinque
     * shell partono sfalsate, la cassetta non si riempie mai e la tastiera
     * funziona. Con l'avvio silenzioso arrivano insieme. Lo stesso floppy,
     * due comportamenti, e la differenza era la VELOCITA' — il tipo di
     * indizio che in emulazione non si produce mai.
     *
     * Adesso: solo -ESRCH e' morte. Tutto il resto si riprova.
     * ===================================================================== */
    {
        int tentativo;

        for (tentativo = 0; tentativo < 50; tentativo++) {
            r = ipc_send(g_kbd_pid, KBD_MSG_READLINE, &richiesta,
                         sizeof(richiesta));
            if (r >= 0) break;
            if (r == -ESRCH) break;         /* quello si': il driver non c'e' piu' */
            sched_sleep(10);                /* pieno: fra poco si svuota */
        }
    }

    if (r < 0) {
        /* -ESRCH, o cinquanta tentativi di seguito con la cassetta piena:
         * a quel punto il driver non sta piu' svuotando, ed e' come se non
         * ci fosse. Riprendiamoci la tastiera, cosi' un driver bloccato
         * degrada la console invece di spegnerla. */
        klog(LOG_ERROR, "TTY: servizio '%s' (PID %u) non raggiungibile (%d) - "
             "ripiego sulla tastiera in-kernel",
             KBD_SERVICE_NAME, g_kbd_pid, r);
        g_kbd_pid = 0;
        tty_set_input_source(TTY_INPUT_INTERNAL);
        return -1;
    }

    /* Attendi la risposta. Scarta (segnalandoli) messaggi di altro tipo:
     * oggi nessun altro scrive nella mailbox della shell, ma il giorno
     * in cui accadesse è meglio perdere un messaggio estraneo che
     * restituirlo al chiamante come se fosse una riga digitata. */
    for (;;) {
        r = ipc_recv(&meta, dst, n);
        if (r < 0) return r;

        if (meta.tipo == KBD_MSG_LINE && meta.sender_pid == g_kbd_pid) {
            uint32_t len = meta.len;
            if (len > n) len = n;   /* ipc_recv ha già troncato la copia */
            return (int)len;
        }

        klog(LOG_WARN, "TTY: messaggio inatteso in attesa di una riga "
             "(type=%u da PID %u), ignorato", meta.tipo, meta.sender_pid);
    }
}

/* =============================================================================
 * tty_read_internal — percorso storico: ring buffer riempito dall'handler
 * IRQ1 in-kernel. Attivo solo in modalità TTY_INPUT_INTERNAL.
 * ============================================================================= */
static int tty_read_internal(char *dst, uint32_t n)
{
    /* Il ripiego si prova a disfare da qui: e' il punto attraversato a ogni
     * lettura mentre si e' caduti sul gestore interno. Vedi la funzione. */
    tty_forse_torna_al_driver();

    uint32_t i = 0;

    while (i < n) {
        /* Aspetta carattere disponibile.
         *
         * RACE RISOLTA (luglio 2026 — "lost wakeup"): la sequenza era
         *     g_waiting_pid = pid;
         *     sched_block(PROC_BLOCKED);
         * con gli interrupt ABILITATI. Se IRQ1 arrivava nella finestra fra
         * le due istruzioni, l'handler trovava g_waiting_pid già impostato
         * e chiamava sched_unblock(pid) su un processo ancora RUNNING
         * (no-op), azzerando poi g_waiting_pid. Subito dopo sched_block()
         * metteva il processo in BLOCKED: la sveglia era già stata
         * consumata e nessuno l'avrebbe più emessa — la shell restava
         * bloccata per sempre al prompt, con la riga già nel ring buffer.
         *
         * Fix: test-and-block atomico rispetto all'IRQ (cli), con
         * ri-controllo del buffer a interrupt disabilitati. sched_block()
         * riabilita gli interrupt al risveglio. */
        for (;;) {
            Process *cur;

            if (g_input_buf.count != 0) break;

            cur = proc_get_current();
            if (cur == NULL) {
                /* Siamo nel kernel fuori dallo scheduler (solo durante il
                 * boot): non c'è un processo da bloccare, attendi l'IRQ. */
                __asm__ volatile ("hlt");
                continue;
            }

            interrupts_disable();
            if (g_input_buf.count != 0) {
                interrupts_enable();
                break;
            }
            g_waiting_pid = cur->pid;
            sched_block(PROC_BLOCKED);   /* riabilita IF al risveglio */

            /* ! ED E' QUI CHE Ctrl+C DIVENTA VISIBILE. Un programma che
             * aspetta un tasto sta esattamente in questo punto: senza questa
             * riga, l'interruzione lo sveglierebbe, lui troverebbe il buffer
             * vuoto e tornerebbe a dormire — e l'unico modo di vederlo morire
             * sarebbe battere qualcos'altro. */
            if (proc_interrotto()) return -EINTR;
        }

        char c = ring_buf_get();
        dst[i++] = c;

        /* Modalità linea: ritorna al \n */
        if (c == '\n' || c == '\r') break;
    }

    return (int)i;
}

/* =============================================================================
 * drv_read — Legge N byte dal TTY (bloccante)
 *
 * Interfaccia invariata per chi la chiama (sys_read su fd 0): ritorna
 * quando la riga è completa (terminatore incluso) o quando il buffer del
 * chiamante è pieno. Cambia solo da dove arrivano i byte.
 * ============================================================================= */
int drv_read(void *buf, size_t n)
{
    if (!buf || n == 0) return -1;

    if (g_input_src == TTY_INPUT_IPC) {
        return tty_read_ipc((char *)buf, n);
    }

    if (g_input_src == TTY_INPUT_NONE) {
        /* Nessuno ha ancora scelto la sorgente: succede solo se qualcuno
         * legge da tastiera prima del PASSO 14b. Non restare appesi su
         * un IRQ che nessuno ha smascherato. */
        klog(LOG_ERROR, "TTY: read prima che la sorgente input fosse scelta");
        return -1;
    }

    return tty_read_internal((char *)buf, n);
}

/* =============================================================================
 * drv_write — Scrive N byte sul TTY (VGA)
 * ============================================================================= */
int drv_write(const void *buf, size_t n)
{
    const char *src = (const char *)buf;
    uint32_t    i;

    if (!buf || n == 0) return 0;

    /* =====================================================================
     * Nessuna interpretazione qui: i byte vanno a vga_putchar così come
     * sono, e le sequenze ANSI le riconosce il suo parser.
     *
     * Fino a luglio 2026 questa funzione ne aveva uno PROPRIO, che
     * intercettava ESC[<n>m prima che i byte arrivassero al VGA. Erano
     * due parser sulla stessa strada, e i problemi erano tre:
     *
     *   - lavorava sul buffer di una singola write(), quindi una
     *     sequenza spezzata a metà fra due chiamate gli sfuggiva;
     *   - conosceva solo il colore, e per cursore e cancellazioni
     *     lasciava passare i byte a vga_putchar — che li scartava. È il
     *     motivo per cui il comando `cls` della shell non ha mai
     *     cancellato niente: emetteva ESC[2J ESC[H e nessuno dei due
     *     parser li implementava;
     *   - aveva una tabella colori tutta sua, diversa da quella di
     *     vga.c, e vinceva la sua (vedi il commento sui CLR_* in
     *     bin/sh/shell.c).
     *
     * Con un parser solo, in vga.c, tutte e tre le cose si sistemano
     * insieme e /bin/gfedit ha un terminale su cui disegnare.
     * ===================================================================== */
    for (i = 0; i < n; i++) {
        vga_putchar(src[i]);
    }

    return (int)n;
}

/* =============================================================================
 * drv_ioctl — Controllo terminale
 * ============================================================================= */
int drv_ioctl(int cmd, void *arg)
{
    switch (cmd) {
        case TTY_IOCTL_GETSIZE: {
            /* Ritorna dimensioni terminale */
            /* ! LA MISURA SI CHIEDE AL VGA, NON SI SCRIVE QUI. Qui c'erano
             * 80x25 e 640x400 come costanti: vero in modo testo, falso su una
             * console disegnata dentro un framebuffer, dove a 800x600 le
             * colonne sono cento e le righe trentasette. Chi ci credeva
             * disegnava su tre quarti di schermo — e `telnet` dichiarava
             * all'altro capo, con NAWS, una finestra che non era la sua. */
            TtyWinSize *ws = (TtyWinSize *)arg;
            uint32_t c, r, pw, ph;

            if (!ws) return -1;
            vga_geometria(&c, &r, &pw, &ph);
            ws->rows   = (unsigned short)r;
            ws->cols   = (unsigned short)c;
            ws->xpixel = (unsigned short)pw;
            ws->ypixel = (unsigned short)ph;
            return 0;
        }
        case TTY_IOCTL_SETRAW: {
            /* =============================================================
             * Modalità raw dal lato USCITA.
             *
             * L'ingresso non passa di qui: eco e line discipline vivono
             * nel driver /dev/kbd.drv, e chi vuole i tasti singoli glielo
             * chiede direttamente via IPC (KBD_MSG_SETMODE, vedi
             * drivers/kbd/kbd_proto.h). Il TTY non ha voce in capitolo e
             * fingere di averla — restituendo 0 senza fare nulla, come
             * faceva prima — dava per buona una richiesta che nessuno
             * eseguiva.
             *
             * Ciò che invece è affare del TTY è lo specchio seriale: per
             * un programma a schermo intero va spento, o ogni ridisegno
             * costa l'attesa del THR carattere per carattere. Il perché
             * in dettaglio è in vga_set_serial_mirror().
             * ============================================================= */
            vga_set_serial_mirror(0);
            klog(LOG_DEBUG, "TTY: uscita in modalita' raw (specchio seriale spento)");
            return 0;
        }
        case TTY_IOCTL_SETCOOKED: {
            vga_set_serial_mirror(1);
            klog(LOG_DEBUG, "TTY: uscita in modalita' cooked (specchio seriale acceso)");
            return 0;
        }
        /* TTY_IOCTL_CLEAR e TTY_IOCTL_SETCOLOR non passano piu' di qui:
         * riguardano UNA console precisa — quella del processo che
         * chiama — e sys_ioctl li risolve da solo, perche' e' li' che il
         * numero di console e' noto. Questa funzione riceve soltanto
         * (comando, argomento) e non ha modo di sapere per conto di chi
         * sta lavorando. */
    }
    return -1;
}

/* =============================================================================
 * drv_exit — Disinstalla il driver TTY
 * ============================================================================= */
void drv_exit(void)
{
    pic_mask_irq(1);
    klog(LOG_INFO, "TTY: driver disinstallato, IRQ1 mascherato");
}

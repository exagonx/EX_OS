/* =============================================================================
 * drivers/mouseser/mouseser.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * MOUSE SERIALE — protocollo Microsoft, 1200 baud 7N1
 *
 *     /dev/mouseser.drv          COM2 (0x2F8, IRQ3) — il predefinito
 *     /dev/mouseser.drv com1     COM1 (0x3F8, IRQ4)
 *     /dev/mouseser.drv -i       la sonda: dice se la UART c'e', esce
 *
 * Registra il servizio "mouse" e parla lo STESSO protocollo del mouse PS/2
 * (drivers/kbd/kbd_proto.h): chi legge il mouse non deve sapere da dove
 * arriva. Il PS/2 sta dentro kbd.drv perche' condivide il controller con la
 * tastiera; questo e' hardware separato, quindi e' un driver separato — ed e'
 * il primo caso in cui il nome del servizio serve davvero a qualcosa.
 *
 * -----------------------------------------------------------------------------
 * ! IL PREDEFINITO E' COM2, E NON E' UNA PREFERENZA
 *
 * COM1 e' la console di log del kernel: `serial_putchar()` in
 * kernel/arch/x86/vga.c ci scrive il log di avvio, e tutte le prove
 * automatiche di questo progetto lo leggono da li'. Un mouse su COM1
 * spegnerebbe lo strumento con cui si verifica che il mouse funziona.
 *
 * -----------------------------------------------------------------------------
 * ! UN MOUSE SERIALE NON SI PUO' SONDARE, e va detto invece che finto
 *
 * Non risponde a nessuna domanda: parla solo quando lo si muove. L'unica cosa
 * rilevabile e' la UART — con la prova del registro di appunti (scratch), che
 * dice se il chip c'e', non se ci sia attaccato un mouse.
 *
 * Alla riaccensione di RTS un mouse Microsoft manda una 'M' per presentarsi, e
 * se arriva la si riferisce. Ma NON e' un requisito: il mouse seriale di QEMU
 * non la manda, e pretenderla vorrebbe dire rifiutare l'unico mouse seriale su
 * cui questo codice si puo' provare.
 *
 * -----------------------------------------------------------------------------
 * IL PROTOCOLLO, tre byte
 *
 *     byte 0   1 X S D  dY1 dY0 dX1 dX0     X = bit 6, sempre 1: e' il primo
 *     byte 1   0 dX5..dX0
 *     byte 2   0 dY5..dY0
 *
 * S = tasto sinistro, D = destro. I due bit alti di ogni spostamento stanno
 * nel primo byte: sono numeri a 8 bit spezzati in due posti, come nel PS/2.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `mouseser.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("mouseser.drv", "0.001");

/* --- UART 16550, spiazzamenti dalla base --------------------------------- */
#define U_DATI      0   /* RBR/THR (e DLL quando DLAB e' acceso) */
#define U_IER       1   /* interrupt enable (e DLM con DLAB)     */
#define U_IIR       2   /* interrupt identification              */
#define U_LCR       3   /* line control (DLAB e' il bit 7)       */
#define U_MCR       4   /* modem control: DTR, RTS, OUT2         */
#define U_LSR       5   /* line status: bit 0 = dato pronto      */
#define U_MSR       6
#define U_SCR       7   /* scratch: un byte di appunti, serve a sondare */

#define LCR_DLAB    0x80
#define LCR_7N1     0x02    /* 7 bit, 1 stop, nessuna parita' */
#define MCR_DTR     0x01
#define MCR_RTS     0x02
#define MCR_OUT2    0x08    /* senza questo l'IRQ non arriva al PIC */
#define LSR_PRONTO  0x01

#define PORTE_N     8

static unsigned int g_base = 0x2F8;     /* COM2 */
static unsigned int g_irq  = 3;

/* Stato consegnato ai client: e' lo stesso di kbd_proto.h. */
static int          g_dx = 0, g_dy = 0;
static unsigned int g_bottoni = 0;
static unsigned int g_persi   = 0;
static unsigned int g_novita  = 0;
static unsigned int g_attesa_pid = 0;
static unsigned int g_visto_M = 0;

static unsigned char g_pkt[3];
static unsigned int  g_pkt_n = 0;

static void fuori(unsigned int off, unsigned int val)
{
    ioport_out(g_base + off, val);
}

static int dentro(unsigned int off)
{
    return ioport_in(g_base + off);
}

/* =============================================================================
 * Il montaggio dei pacchetti
 * ========================================================================== */
static void byte_arrivato(unsigned char b)
{
    /* La presentazione: una 'M' subito dopo la riaccensione di RTS. Non e'
     * obbligatoria (vedi in testa), quindi si annota e si va avanti. */
    if (g_pkt_n == 0 && b == 'M') { g_visto_M = 1; return; }

    /* ! IL PRIMO BYTE E' L'UNICO CON IL BIT 6 ACCESO. E' l'unico modo di
     * ritrovare il passo dopo un byte perso: senza, un solo byte mancante
     * sfaserebbe tutti i pacchetti successivi e il puntatore andrebbe a caso
     * per sempre invece che per un istante. */
    if (g_pkt_n == 0 && !(b & 0x40)) { g_persi++; return; }
    if (g_pkt_n > 0  &&  (b & 0x40)) {
        /* Un primo byte dove ne aspettavo uno di continuazione: il pacchetto
         * di prima era monco. Si ricomincia da questo invece di buttarlo. */
        g_persi++;
        g_pkt_n = 0;
    }

    g_pkt[g_pkt_n++] = b;
    if (g_pkt_n < 3) return;
    g_pkt_n = 0;

    {
        int dx = (int)(((g_pkt[0] & 0x03) << 6) | (g_pkt[1] & 0x3F));
        int dy = (int)(((g_pkt[0] & 0x0C) << 4) | (g_pkt[2] & 0x3F));

        /* Numeri a 8 bit con segno, ricomposti da due pezzi. */
        if (dx > 127) dx -= 256;
        if (dy > 127) dy -= 256;

        g_dx += dx;
        /* ! LA Y NON SI GIRA QUI. Il protocollo Microsoft la manda gia'
         * positiva verso il BASSO, al contrario del PS/2. Girarla «per
         * simmetria» con l'altro driver darebbe un puntatore che va
         * dalla parte sbagliata solo con il mouse seriale. */
        g_dy += dy;

        g_bottoni = 0;
        if (g_pkt[0] & 0x20) g_bottoni |= MOUSE_BTN_SIN;
        if (g_pkt[0] & 0x10) g_bottoni |= MOUSE_BTN_DES;
        g_novita = 1;
    }
}

static void svuota_uart(void)
{
    int guard;

    for (guard = 0; guard < 64; guard++) {
        int st = dentro(U_LSR);
        if (st < 0 || !(st & LSR_PRONTO)) break;
        {
            int d = dentro(U_DATI);
            if (d < 0) break;
            byte_arrivato((unsigned char)d);
        }
    }
}

/* =============================================================================
 * Accensione
 * ========================================================================== */
static int uart_c_e(void)
{
    /* Prova del registro di appunti: si scrive un valore, lo si rilegge. Un
     * indirizzo senza chip legge 0xFF e non conserva niente. Dice che c'e' la
     * UART, NON che ci sia attaccato un mouse. */
    fuori(U_SCR, 0x5A);
    if (dentro(U_SCR) != 0x5A) return 0;
    fuori(U_SCR, 0xA5);
    if (dentro(U_SCR) != 0xA5) return 0;
    return 1;
}

static void uart_init(void)
{
    fuori(U_IER, 0x00);                 /* niente interrupt mentre si configura */

    fuori(U_LCR, LCR_DLAB);
    fuori(U_DATI, 96);                  /* 115200 / 96 = 1200 baud */
    fuori(U_IER, 0);                    /* meta' alta del divisore */
    fuori(U_LCR, LCR_7N1);

    /* ! RTS SPENTO E POI ACCESO E' IL RESET DEL MOUSE, e non c'e' un altro
     * modo: il mouse seriale si alimenta dalle linee di controllo. Spegnerle
     * lo spegne davvero; riaccenderle lo fa ripartire, ed e' allora che manda
     * la 'M'. */
    fuori(U_MCR, 0x00);
    usleep(20000);
    fuori(U_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
    usleep(20000);

    svuota_uart();                      /* la 'M', se e' arrivata */

    fuori(U_IER, 0x01);                 /* adesso si', dato disponibile */
}

/* =============================================================================
 * Servizio
 * ========================================================================== */
static void rispondi(unsigned int pid)
{
    MouseStato s;

    s.dx       = g_dx;
    s.dy       = g_dy;
    s.bottoni  = g_bottoni;
    s.presente = 1;                     /* la UART c'e': oltre non si sa */
    s.persi    = g_persi;

    if (ipc_send(pid, MOUSE_MSG_STATO, &s, sizeof(s)) < 0) return;

    /* Solo a consegna riuscita: azzerare prima vorrebbe dire buttare uno
     * spostamento che il client non ha mai ricevuto. */
    g_dx = 0; g_dy = 0; g_persi = 0; g_novita = 0;
}

int main(int argc, char **argv)
{
    IpcMessage   meta;
    unsigned char payload[64];
    int rc, sonda = 0, i;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'i') sonda = 1;
        else if (strcmp(argv[i], "com1") == 0) { g_base = 0x3F8; g_irq = 4; }
        else if (strcmp(argv[i], "com2") == 0) { g_base = 0x2F8; g_irq = 3; }
    }

    rc = ioport_bind(g_base, PORTE_N);
    if (rc < 0) {
        printf("mouseser: ioport_bind(0x%x, %d) fallita (%d)\n",
               g_base, PORTE_N, rc);
        return 1;
    }

    if (!uart_c_e()) {
        printf("mouseser: nessuna UART a 0x%x.\n", g_base);
        return 1;
    }

    if (sonda) {
        printf("mouseser: UART a 0x%x (IRQ%u)\n", g_base, g_irq);
        return 0;
    }

    if (g_base == 0x3F8) {
        printf("mouseser: ATTENZIONE, COM1 e' la console di log del kernel.\n");
    }

    uart_init();

    rc = ipc_register("mouse");
    if (rc < 0) {
        printf("mouseser: ipc_register('mouse') fallita (%d) - esco\n", rc);
        return 1;
    }

    rc = irq_bind(g_irq);
    if (rc < 0) {
        printf("mouseser: irq_bind(%u) fallita (%d) - esco\n", g_irq, rc);
        return 1;
    }

    /* Un byte arrivato fra uart_init() e irq_bind() non genererebbe piu'
     * nessuna notifica: si guarda una volta a mano. */
    svuota_uart();

    printf("mouseser: servizio 'mouse' attivo su 0x%x IRQ%u%s\n",
           g_base, g_irq, g_visto_M ? " (il mouse si e' presentato)" : "");

    for (;;) {
        if (ipc_recv(&meta, payload, sizeof(payload)) < 0) continue;

        if (meta.sender_pid == IPC_SENDER_KERNEL &&
            meta.tipo == IPC_TYPE_IRQ_NOTIFY) {
            svuota_uart();
            irq_done(g_irq);

            if (g_attesa_pid != 0 && g_novita) {
                unsigned int p = g_attesa_pid;
                g_attesa_pid = 0;
                rispondi(p);
            }
            continue;
        }

        if (meta.tipo == MOUSE_MSG_LEGGI) {
            unsigned int attendi = 0;

            if (meta.len >= sizeof(unsigned int))
                memcpy(&attendi, payload, sizeof(unsigned int));

            if (!attendi || g_novita) rispondi(meta.sender_pid);
            else                      g_attesa_pid = meta.sender_pid;
            continue;
        }
    }
}

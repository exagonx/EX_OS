/* =============================================================================
 * drivers/kbd/kbd.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Driver tastiera PS/2 di EX-OS (/dev/kbd.drv) — PROCESSO RING3.
 *
 * Primo driver della migrazione a userspace. Fino a luglio 2026 questo
 * file era un modulo ELF ET_DYN mappato nello spazio del KERNEL da
 * drvmgr.c/dynlink.c: usava port_inb/port_outb diretti e chiamava
 * simboli del kernel (klog, irq_register_handler, sched_unblock) per
 * linkage diretto. Girava quindi in ring0 a tutti gli effetti: un suo
 * bug poteva corrompere il kernel.
 *
 * Ora è un normale eseguibile ET_EXEC statico, identico nello schema a
 * /bin/ls (start.S + libc, vedi Makefile), caricato al PASSO 14b di
 * kernel_main come processo ring3 con la propria page directory. Non
 * esegue nessuna istruzione privilegiata: tutto l'hardware passa dal
 * kernel, che fa da mediatore e da guardia.
 *
 *   port_inb/port_outb   ->  ioport_in/ioport_out  (SYS_IOPORT_IN/OUT),
 *                            consentite solo dentro il range dichiarato
 *                            una volta con ioport_bind()
 *   irq_register_handler ->  irq_bind(1) (SYS_IRQ_BIND): da quel momento
 *                            ogni IRQ1 arriva come messaggio IPC nella
 *                            mailbox di questo processo
 *   sched_unblock(pid)   ->  ipc_send() al client in attesa
 *   klog()               ->  printf() su stdout (fd 1 = TTY del kernel)
 *
 * Conseguenza di progetto importante: NON esiste più un "handler IRQ1"
 * che gira in contesto interrupt. C'è un solo flusso di esecuzione — il
 * loop di servizio in main() — che alterna il consumo degli scancode e
 * la risposta ai client. Tutte le race del vecchio modello (buffer
 * condiviso fra handler e contesto processo, lost wakeup, necessità di
 * `volatile`) semplicemente non esistono più: nessuna variabile di
 * questo file è toccata da due contesti diversi.
 *
 * Line discipline: resta qui, non nel kernel. Il driver accumula la riga
 * in g_line, gestisce Backspace, fa l'eco a video con write(1, ...) e
 * consegna la riga al client solo su Invio. È la stessa semantica
 * "cooked" che il TTY in-kernel implementava da solo, spostata di lato
 * senza cambiarla — vedi drivers/tty/tty.c per il perché l'eco e il
 * Backspace vanno gestiti PRIMA della consegna e non dopo.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"

/* =============================================================================
 * Porte KBC (Keyboard Controller 8042)
 *
 * Range rivendicato con ioport_bind(): 0x60..0x64. Il kernel rifiuta con
 * -EPERM qualunque ioport_in/out fuori da qui, quindi un bug in questo
 * file non può toccare il PIC, il PIT o il floppy.
 * ============================================================================= */
#define KBC_DATA    0x60    /* Data port (R/W) */
#define KBC_STATUS  0x64    /* Status register (R) */
#define KBC_CMD     0x64    /* Command register (W) */
#define KBC_PORT_BASE   KBC_DATA
#define KBC_PORT_COUNT  5   /* 0x60,0x61,0x62,0x63,0x64 */

/* Bit Status Register */
#define KBC_OBF     0x01    /* Output Buffer Full — dato disponibile in 0x60 */
#define KBC_IBF     0x02    /* Input Buffer Full — controller occupato */
#define KBC_AUX     0x20    /* Il byte in 0x60 viene dalla seconda porta (mouse) */

/* Comandi KBC */
#define KBC_CMD_SELF_TEST   0xAA    /* Self test */
#define KBC_CMD_KBD_ENABLE  0xAE    /* Abilita interfaccia tastiera */
#define KBC_CMD_READ_CFG    0x20    /* Leggi il configuration byte */
#define KBC_CMD_WRITE_CFG   0x60    /* Scrivi il configuration byte */
#define KBC_CMD_SET_LEDS    0xED    /* Imposta LED (inviato alla tastiera via 0x60) */
#define KBC_CMD_ENABLE_SCAN 0xF4    /* Abilita scansione */

/* Bit del configuration byte dell'8042 */
#define KBC_CFG_KBD_INT     0x01    /* 1 = il KBC alza IRQ1 quando arriva un byte */
#define KBC_CFG_KBD_CLOCK   0x10    /* 1 = clock della tastiera DISABILITATO */
#define KBC_CFG_TRANSLATE   0x40    /* 1 = traduci gli scancode in set 1 */

/* Configurazione di ripiego se la lettura del byte corrente fallisce:
 * IRQ1 attivo, system flag, traduzione in set 1. Il resto a zero. */
#define KBC_CFG_FALLBACK    0x45

/* --- La seconda porta PS/2: il mouse. Vedi kbd_proto.h per il perche' sta
 * dentro questo driver e non in uno suo. --------------------------------- */
#define KBC_CMD_AUX_ENABLE  0xA8    /* abilita la seconda porta */
#define KBC_CMD_AUX_SCRIVI  0xD4    /* il byte dopo va AL MOUSE, non al KBC */
#define KBC_CFG_AUX_INT     0x02    /* 1 = il KBC alza IRQ12 per la seconda porta */
#define KBC_CFG_AUX_CLOCK   0x20    /* 1 = clock della seconda porta DISABILITATO */

#define MOUSE_IRQ           12
#define MOUSE_CMD_RESET     0xFF
#define MOUSE_CMD_DEFAULTS  0xF6
#define MOUSE_CMD_REPORT_ON 0xF4
#define MOUSE_ACK           0xFA

/* IRQ della tastiera PS/2 */
#define KBD_IRQ     1

/* Le costanti di attesa stanno più sotto, insieme a kbc_wait_read_ms().
 * Qui c'era KBC_POLL_MAX=2000, un limite a CONTEGGIO DI ITERAZIONI, con
 * la motivazione "il KBC risponde in decine di microsecondi": vera per un
 * registro, falsa per il self-test 0xAA. Vedi il commento là. */

/* =============================================================================
 * Disposizione della tastiera
 *
 * Le tabelle stanno in keymaps.h, una per disposizione. Qui si tiene solo
 * QUALE e' attiva: il resto del driver indicizza sempre allo stesso modo,
 * perche' l'indice e' il tasto fisico e quello non cambia.
 *
 * ! SI PARTE DA `us` E NON DA NIENTE. Se la voce in kernel.cfg manca o
 * nomina una disposizione sconosciuta, la tastiera deve comunque scrivere:
 * una tastiera muta perche' la configurazione ha un refuso e' un sistema
 * che non si puo' nemmeno usare per correggere quel refuso.
 * ============================================================================= */
#include "keymaps.h"

static const Keymap *g_map = &g_keymaps[0];   /* us */


static int keymap_scegli(const char *nome)
{
    int i;

    if (nome == NULL || nome[0] == '\0') return -1;

    for (i = 0; i < KEYMAP_N; i++) {
        const char *a = g_keymaps[i].nome, *b = nome;

        while (*a && *b) {
            char x = *a, y = *b;

            if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
            if (x != y) break;
            a++; b++;
        }
        if (*a == '\0' && (*b == '\0' || *b == '\n' || *b == ' ')) {
            g_map = &g_keymaps[i];
            return i;
        }
    }
    return -1;
}

/* =============================================================================
 * Stato del driver
 *
 * Niente `volatile` e niente sezioni critiche: a differenza del vecchio
 * driver (e del TTY in-kernel) qui non esiste un handler di interrupt.
 * Ogni riga di questo file gira nell'unico thread di main().
 * ============================================================================= */

/* Modificatori */
static unsigned char g_shift = 0;
static unsigned char g_ctrl  = 0;
static unsigned char g_alt   = 0;
static unsigned char g_altgr = 0;   /* Alt di DESTRA: e0 38 */

/* Dallo scancode al carattere, secondo la disposizione attiva.
 *
 * ! UN PUNTO SOLO PER LE DUE MODALITA'. Cooked e raw traducono lo stesso
 * tasto e devono ottenere lo stesso carattere: due copie di questa
 * funzione darebbero un editor a schermo intero che scrive le graffe e una
 * shell che non le scrive, o il contrario — e nessuno dei due sintomi
 * suggerirebbe la causa.
 *
 * ! L'ORDINE DELLE TABELLE NON E' ARBITRARIO: si prova la piu' specifica
 * per prima, AltGr+Shift, poi AltGr, poi Shift, poi il tasto nudo. Una
 * casella vuota nella tabella specifica NON e' un tasto muto: e' «qui non
 * c'e' niente di speciale», e si ricade su quella sotto. Senza, AltGr+A
 * smetterebbe di scrivere una A. */
static char traduci(unsigned char sc)
{
    char a = 0;

    if (sc >= KEYMAP_N_TASTI) return 0;

    if (g_altgr && g_shift) a = (char)g_map->altgr_sh[sc];
    if (!a && g_altgr)      a = (char)g_map->altgr[sc];
    if (!a && g_shift)      a = (char)g_map->shift[sc];
    if (!a && !g_altgr)     a = (char)g_map->normale[sc];

    /* Il tasto in piu' delle tastiere a 102 tasti, che nelle tabelle non
     * c'e' perche' sta fuori dalle righe: vedi keymaps.h. */
    if (!a && sc == KEYMAP_TASTO_102)
        a = (char)keymap_102(g_map, g_shift, g_altgr);

    return a;
}

static unsigned char g_caps  = 0;
static unsigned char g_e0    = 0;   /* prefisso tasto esteso 0xE0 */
static unsigned char g_leds  = 0;   /* bit0=Scroll, bit1=Num, bit2=Caps */

/* =============================================================================
 * Stato di input, UNO PER CONSOLE
 *
 * Con le console virtuali il driver non serve più "la console": ne
 * serve quattro, di cui una sola in primo piano. Tutto ciò che riguarda
 * il testo digitato — la riga in costruzione, il type-ahead, chi sta
 * aspettando, la modalità — appartiene a una console precisa, e tenerlo
 * in variabili globali significherebbe che la shell della console 2
 * riceve i tasti battuti sulla console 1.
 *
 * Restano globali soltanto i modificatori (Shift, Ctrl, Alt, CapsLock):
 * quelli sono stato FISICO della tastiera, non input accumulato, e la
 * tastiera è una sola.
 * ============================================================================= */
#define KBD_RING_SIZE   512     /* type-ahead per console, in byte */
#define KBD_KEYRING_SIZE 32     /* eventi tasto in coda, per console */

typedef struct {
    /* Riga in costruzione (line discipline "cooked") */
    char     line[KBD_LINE_MAX];
    unsigned line_len;

    /* Per ogni carattere del buffer: l'abbiamo disegnato o no?
     *
     * ! NON SI PUO' RICAVARE DAL CARATTERE. Le frecce entrano nella riga
     * come sequenza ANSI "ESC [ A": due dei tre byte sono stampabili, e
     * nessuno dei tre e' stato ecoato. Chiederlo al carattere darebbe due
     * colonne da cancellare che non esistono — di nuovo il prompt. */
    unsigned char vis[KBD_LINE_MAX];

    /* =====================================================================
     * QUANTE COLONNE DI SCHERMO ABBIAMO DAVVERO DISEGNATO PER QUESTA RIGA
     *
     * ! NON E' line_len, E CONFONDERLE MANGIA IL PROMPT.
     *
     * Un carattere di controllo — ESC, Ctrl+lettera — entra nel buffer di
     * riga ma NON viene ecoato (lo si spiega piu' sotto). Il Backspace
     * lavorava su line_len ed emetteva "\b \b" per ognuno: due ESC
     * battuti per sbaglio, due Backspace, e i due caratteri cancellati
     * erano gli ultimi del PROMPT, che non appartiene a noi.
     *
     * E' successo davvero, ed e' visibile nel registro seriale: al prompt
     * di textline restava "^H ^H^H ^H" e l'asterisco spariva.
     *
     * Con questo contatore il Backspace cancella una colonna solo se
     * quella colonna l'abbiamo scritta noi. Il prompt e' fuori portata
     * per costruzione, non per un controllo in piu'.
     * ===================================================================== */
    unsigned line_col;

    /* Ring delle righe già completate ma non ancora richieste da nessuno
     * (type-ahead: l'utente digita mentre il client sta ancora eseguendo
     * il comando precedente). Contiene i byte delle righe, '\n' incluso;
     * rlines conta quanti '\n' ci sono, cioè quante righe complete sono
     * disponibili. */
    char     ring[KBD_RING_SIZE];
    unsigned rhead, rtail, rcount, rlines;

    /* Client in attesa di una RIGA (0 = nessuno) */
    unsigned reader_pid;
    unsigned reader_max;

    /* =====================================================================
     * Modalità raw — vedi il commento esteso in kbd_proto.h
     *
     * Le due modalità condividono la traduzione degli scancode fino ai
     * modificatori, poi divergono del tutto: in cooked il risultato è un
     * carattere che entra in line (con eco e Backspace), in raw è un
     * evento che entra in keys così com'è.
     *
     * Il ring degli eventi è piccolo di proposito. Il type-ahead di una
     * console a righe ha senso — si digita il comando successivo mentre
     * il precedente lavora — mentre trentadue tasti accumulati davanti a
     * un editor che non li ha ancora letti sono già una raffica che
     * l'utente non ha voluto: meglio perderne la coda che ripeterla a
     * schermo dopo secondi. Serve solo a coprire la finestra fra un
     * READKEY e il successivo.
     * ===================================================================== */
    unsigned char raw;
    unsigned keys[KBD_KEYRING_SIZE];
    unsigned khead, ktail, kcount;

    /* Client in attesa di un TASTO. Distinto da reader_pid: sono due
     * protocolli diversi e tenerli separati evita di consegnare una riga
     * a chi aveva chiesto un tasto. */
    unsigned keyreader_pid;
} ConsoleIn;

static ConsoleIn g_c[KBD_N_CONSOLE];

/* Console in primo piano. È la sola che riceve i tasti: le altre hanno
 * i propri lettori in attesa, fermi finché non tornano davanti. */
static unsigned g_attiva = 0;

/* =============================================================================
 * Eco a video — sulla console di chi sta digitando, non su quella del
 * driver.
 *
 * Prima era una write(1, ...), che finiva sullo stdout del processo kbd
 * — cioè sulla console 0. Con una console sola coincidevano; con
 * quattro, l'eco dei tasti battuti sulla console 2 sarebbe comparso
 * sulla 0, e la 2 sarebbe rimasta muta mentre l'utente digitava.
 *
 * console_write() e' la syscall che esiste apposta per questo caso:
 * scrivere su una console che non e' la propria. Vedi SYS_CONSOLE_WRITE
 * in kernel/include/syscall.h.
 * ============================================================================= */
static void echo(const char *s, unsigned n)
{
    console_write(g_attiva, s, n);
}

static void echo_char(char c)
{
    echo(&c, 1);
}

/* =============================================================================
 * eco_visibile — questo carattere occupa esattamente una colonna?
 *
 * E' l'unico posto in cui si decide che cosa finisce sullo schermo, ed e'
 * la stessa domanda che il Backspace deve rifare al contrario. Tenerla in
 * una funzione sola e' la ragione per cui le due risposte non possono
 * divergere: quando divergono, si cancella il prompt.
 *
 * ! I CARATTERI DI CONTROLLO NON SI ECOANO MA SI ACCUMULANO. ESC,
 * Ctrl+lettera e simili entrano nel buffer di riga — /bin/textline usa
 * ESC per annullare la riga in inserimento e deve riceverlo — ma la VGA
 * li renderebbe come glifi casuali della code page 437.
 *
 * ! IL TAB NON SI ECOA PIU'. Prima si', "perche' a video ha un effetto
 * sensato": vero finche' non si preme Backspace. Un tab avanza fino alla
 * prossima tabulazione, cioe' di un numero di colonne che dipende da dove
 * comincia il prompt — e questo driver la colonna assoluta non la sa.
 * Disegnare qualcosa che non si sa disfare e' precisamente il difetto che
 * questa funzione esiste per chiudere.
 * ============================================================================= */
static int eco_visibile(char c)
{
    unsigned char u = (unsigned char)c;

    /* ! ANCHE SOPRA 127, da quando ci sono le disposizioni non inglesi.
     * La `à` e' il byte 0x85 e la VGA le disegna un glifo vero, largo una
     * colonna come tutti gli altri: escluderla la renderebbe un carattere
     * invisibile dentro il comando — cioe' esattamente il difetto che
     * questa funzione esiste per chiudere. 0x7F (DEL) resta fuori: quello
     * un glifo sensato non ce l'ha. */
    return (u >= 32 && u != 127);
}

/* =============================================================================
 * Ring buffer delle righe complete
 * ============================================================================= */
static void ring_put_line(ConsoleIn *c, const char *s, unsigned len)
{
    unsigned i;

    /* O entra tutta la riga (terminatore compreso) o non entra affatto:
     * una riga troncata a metà resterebbe senza '\n' e g_rlines
     * mentirebbe a ring_take_line, che consumerebbe anche la riga
     * successiva credendola la stessa. Meglio perdere la riga in
     * overflow che disallineare il buffer. */
    if (len + 1 > KBD_RING_SIZE - c->rcount) return;

    for (i = 0; i < len; i++) {
        c->ring[c->rtail] = s[i];
        c->rtail = (c->rtail + 1) % KBD_RING_SIZE;
        c->rcount++;
    }
    c->ring[c->rtail] = '\n';
    c->rtail = (c->rtail + 1) % KBD_RING_SIZE;
    c->rcount++;
    c->rlines++;
}

/* Estrae la prossima riga completa. Copia al più 'max' byte in out; i
 * byte eccedenti vengono comunque consumati dal ring (la riga non resta
 * a metà). Ritorna il numero di byte copiati. */
static unsigned ring_take_line(ConsoleIn *c, char *out, unsigned max)
{
    unsigned n = 0;

    if (c->rlines == 0) return 0;

    while (c->rcount > 0) {
        char ch = c->ring[c->rhead];
        c->rhead = (c->rhead + 1) % KBD_RING_SIZE;
        c->rcount--;
        if (n < max) out[n++] = ch;
        if (ch == '\n') break;
    }
    c->rlines--;
    return n;
}

/* =============================================================================
 * try_serve_reader — consegna una riga al client in attesa su una
 * console, se ce n'è una pronta. Chiamata sia quando arriva una
 * richiesta (potrebbe esserci già type-ahead in coda) sia quando una
 * riga si completa (il client potrebbe essere già in attesa da prima).
 * ============================================================================= */
static void try_serve_reader(unsigned n)
{
    ConsoleIn *c = &g_c[n];
    char       out[KBD_LINE_MAX];
    unsigned   max = c->reader_max;
    unsigned   len;

    if (c->reader_pid == 0 || c->rlines == 0) return;

    if (max > sizeof(out)) max = sizeof(out);
    len = ring_take_line(c, out, max);

    if (ipc_send(c->reader_pid, KBD_MSG_LINE, out, len) < 0) {
        /* Client sparito fra la richiesta e la risposta (terminato, o
         * ucciso da un fault): la riga è persa, ma il driver resta vivo
         * e pronto per il prossimo. */
        printf("kbd: consegna a PID %u fallita, client sparito\n", c->reader_pid);
    }
    c->reader_pid = 0;
}

/* =============================================================================
 * Ring degli eventi tasto (modalità raw)
 * ============================================================================= */
static void key_put(ConsoleIn *c, unsigned key)
{
    if (c->kcount >= KBD_KEYRING_SIZE) return;   /* raffica: si perde la coda */

    c->keys[c->ktail] = key;
    c->ktail = (c->ktail + 1) % KBD_KEYRING_SIZE;
    c->kcount++;
}

/* =============================================================================
 * try_serve_keyreader — gemello di try_serve_reader per la modalità raw.
 *
 * Il fallimento della consegna qui vale più che nel caso a righe: se il
 * programma a schermo intero è morto senza rimettere la console in
 * cooked, questo è il momento in cui ce ne accorgiamo, ed è l'ultimo
 * utile per non lasciare quella console muta. Vedi kbd_proto.h.
 * ============================================================================= */
static void kbd_set_mode(unsigned n, unsigned mode);

static void try_serve_keyreader(unsigned n)
{
    ConsoleIn *c = &g_c[n];
    unsigned   key;

    if (c->keyreader_pid == 0 || c->kcount == 0) return;

    key = c->keys[c->khead];
    c->khead = (c->khead + 1) % KBD_KEYRING_SIZE;
    c->kcount--;

    if (ipc_send(c->keyreader_pid, KBD_MSG_KEY, &key, sizeof(key)) < 0) {
        printf("kbd: consegna tasto a PID %u fallita, console %u torna cooked\n",
               c->keyreader_pid, n);
        c->keyreader_pid = 0;
        kbd_set_mode(n, KBD_MODE_COOKED);
        return;
    }
    c->keyreader_pid = 0;
}

/* =============================================================================
 * kbd_set_mode — passa fra riga e tasto singolo, su una console.
 *
 * Butta via lo stato di input accumulato in ENTRAMBE le direzioni: la
 * riga a metà e il type-ahead sono testo raccolto con la line discipline
 * cooked, gli eventi in coda sono tasti raccolti senza; consegnare gli
 * uni con le regole degli altri darebbe input inventato.
 * ============================================================================= */
static void kbd_set_mode(unsigned n, unsigned mode)
{
    ConsoleIn    *c     = &g_c[n];
    unsigned char nuovo = (mode == KBD_MODE_RAW) ? 1 : 0;

    if (nuovo == c->raw) return;

    c->raw      = nuovo;
    c->line_len = 0;
    c->line_col = 0;
    c->rhead = c->rtail = c->rcount = c->rlines = 0;
    c->khead = c->ktail = c->kcount = 0;
    g_e0        = 0;

    /* I modificatori NON si azzerano: sono stato fisico della tastiera,
     * non input accumulato. Se l'utente tiene premuto Shift mentre il
     * programma cambia modalità, Shift è ancora premuto. */
}

/* =============================================================================
 * kbd_commuta — Alt+Fn: porta in primo piano un'altra console
 *
 * Due cose insieme, e devono restare insieme: si dice al kernel di
 * mostrare l'altro schermo, e si sposta il proprio 'g_attiva' perché i
 * tasti successivi vadano a chi ora è davanti. Se le due divergessero,
 * si vedrebbe una console e si scriverebbe su un'altra.
 *
 * Il tasto NON viene consegnato a nessuno: è un comando all'interfaccia,
 * non input per il programma in esecuzione. Vedi KBD_ALT_FN_COMMUTA in
 * kbd_proto.h.
 *
 * Chi stava aspettando sulla console che entra viene servito subito, se
 * ha del type-ahead in coda: potrebbe aver chiesto una riga molto prima
 * ed essere rimasto fermo tutto il tempo in cui era nascosto.
 * ============================================================================= */
static void kbd_commuta(unsigned n)
{
    if (n >= KBD_N_CONSOLE || n == g_attiva) return;

    if (console_switch(n) < 0) return;

    g_attiva = n;

    /* I modificatori restano premuti fisicamente (l'utente sta ancora
     * tenendo giù Alt), ma un prefisso di tasto esteso a metà appartiene
     * alla sequenza appena consumata e non va portato di là. */
    g_e0 = 0;

    try_serve_reader(g_attiva);
    try_serve_keyreader(g_attiva);
}

/* =============================================================================
 * Traduzione degli scancode in eventi tasto (solo modalità raw)
 *
 * Ritorna il codice base (>0) o 0 se lo scancode non produce un evento.
 * I modificatori li aggiunge il chiamante: qui si guarda solo il tasto.
 * ============================================================================= */

/* Tasti di navigazione, sia nella versione estesa (0xE0 + codice, i
 * tasti dedicati) sia in quella del tastierino numerico (stesso codice
 * senza prefisso, quando NumLock è spento). Il tastierino manda gli
 * stessi scancode perché è da lì che quei tasti vengono storicamente:
 * il blocco dedicato è un'aggiunta dell'AT esteso.
 *
 * Fuori da questa tabella restano di proposito 0x4A e 0x4E — sul
 * tastierino sono '-' e '+', e sc_normal li mappa già come tali. */
static unsigned kbd_nav_key(unsigned char sc)
{
    switch (sc) {
        case 0x47: return KBD_K_HOME;
        case 0x48: return KBD_K_UP;
        case 0x49: return KBD_K_PGUP;
        case 0x4B: return KBD_K_LEFT;
        case 0x4D: return KBD_K_RIGHT;
        case 0x4F: return KBD_K_END;
        case 0x50: return KBD_K_DOWN;
        case 0x51: return KBD_K_PGDN;
        case 0x52: return KBD_K_INS;
        case 0x53: return KBD_K_DEL;
        default:   return 0;
    }
}

static unsigned kbd_func_key(unsigned char sc)
{
    if (sc >= 0x3B && sc <= 0x44) return KBD_K_F((unsigned)(sc - 0x3B) + 1u);
    if (sc == 0x57)               return KBD_K_F(11);
    if (sc == 0x58)               return KBD_K_F(12);
    return 0;
}

/* Modificatori correnti in forma di maschera, da comporre con il codice base. */
static unsigned kbd_mods(void)
{
    unsigned m = 0;
    if (g_shift) m |= KBD_MOD_SHIFT;
    if (g_ctrl)  m |= KBD_MOD_CTRL;
    if (g_alt)   m |= KBD_MOD_ALT;
    return m;
}

/* =============================================================================
 * kbd_raw_scancode — percorso raw, chiamato al posto della line discipline.
 *
 * Il prefisso 0xE0 e i rilasci sono già stati consumati dal chiamante:
 * qui arriva una pressione, con g_e0 che dice se era estesa.
 * ============================================================================= */
static void kbd_raw_scancode(unsigned char sc, unsigned char esteso)
{
    ConsoleIn *c = &g_c[g_attiva];
    unsigned   base;
    char       ascii;

    /* Tasti estesi: solo navigazione. Il tastierino in versione estesa
     * manda anche 0x35 ('/') e 0x1C (Invio), che sono caratteri normali
     * e cadono giù nel percorso ASCII. */
    if (esteso) {
        base = kbd_nav_key(sc);
        if (base == 0) {
            if (sc == 0x35) base = '/';
            else if (sc == 0x1C) base = '\n';
            else return;
        }
        key_put(c, base | kbd_mods());
        return;
    }

    base = kbd_func_key(sc);
    if (base != 0) { key_put(c, base | kbd_mods()); return; }

    /* Navigazione dal tastierino (NumLock spento): sc_normal ha 0 per
     * questi codici, quindi non si sta rubando nessun carattere. */
    base = kbd_nav_key(sc);
    if (base != 0) { key_put(c, base | kbd_mods()); return; }

    if (sc >= 128) return;

    ascii = traduci(sc);
    if (g_caps && ascii >= 'a' && ascii <= 'z')      ascii = (char)(ascii - 32);
    else if (g_caps && ascii >= 'A' && ascii <= 'Z') ascii = (char)(ascii + 32);

    if (ascii == 0) return;

    /* Nessuna trasformazione Ctrl: il modificatore viaggia a parte. */
    key_put(c, (unsigned)(unsigned char)ascii | kbd_mods());
}

/* =============================================================================
 * ATTESE SUL KBC: PRIMA IL GIRO VELOCE, POI IL TEMPO REALE
 *
 * BUG REALE-HARDWARE (2026-07-31). Queste due funzioni contavano
 * ITERAZIONI, non tempo. Sul Pentium II il risultato e' stato:
 *
 *     kbd: self-test KBC fallito (0xffffffff), continuo
 *     kbd: lettura del configuration byte fallita, uso il ripiego 0x45
 *     kbd: ACK enable-scan non ricevuto (0x30)
 *
 * cioe' tre timeout in fila (0xffffffff e' -1 stampato con %x), e il terzo
 * ha letto un byte sfasato perche' la risposta precedente e' arrivata dopo
 * che avevamo smesso di aspettarla.
 *
 * L'errore stava nella stima scritta nel commento originale: "il KBC
 * risponde in decine di microsecondi". E' vero per la lettura di un
 * registro, ed e' FALSO per il self-test 0xAA — l'8042 e' un
 * microcontrollore che a quel comando esegue la propria routine di
 * diagnostica interna e ci mette MILLISECONDI, non microsecondi. QEMU
 * risponde invece all'istante, quindi 2000 iterazioni bastavano sempre.
 *
 * E' esattamente la stessa famiglia di difetti gia' corretta a giugno nel
 * driver FDC (i loop di NOP a conteggio fisso tarati implicitamente sulla
 * CPU virtuale): un'attesa che dipende dalla velocita' della CPU invece
 * che dall'orologio. Qui era mascherata dal fatto di contare syscall
 * invece di NOP, ma la sostanza e' identica.
 *
 * Struttura in due fasi, perche' i due regimi sono davvero diversi:
 *   - giro veloce a vuoto: copre il caso normale (registro di stato, ACK
 *     di un comando) senza pagare una sola syscall di sleep;
 *   - poi attesa in tempo reale con usleep(1000), che passa da SYS_SLEEP
 *     ed e' quindi ancorata al PIT: indipendente dal clock della CPU.
 *
 * Il chiamante dichiara quanto e' disposto ad aspettare, perche' un ACK e
 * un self-test non sono la stessa cosa.
 * ============================================================================= */

/* Giro veloce prima di iniziare a dormire. */
#define KBC_POLL_FAST   2000

/* =============================================================================
 * ATTESE A SCADENZA REALE (kernel 0.120)
 *
 * Queste attese hanno gia' sbagliato due volte, e vale la pena avere
 * entrambe le storie sotto gli occhi perche' l'errore e' lo stesso visto
 * da due lati:
 *
 *   0.117 e prima — si contavano ITERAZIONI (KBC_POLL_MAX = 2000), sulla
 *     premessa "il KBC risponde in decine di microsecondi". Vera per la
 *     lettura di un registro, falsa per il self-test 0xAA: l'8042 esegue
 *     una diagnostica interna che dura millisecondi. Sul Pentium II tre
 *     timeout in fila; in QEMU mai, perche' li' risponde all'istante.
 *
 *   0.118 — ciclo di usleep(1000) credendo di aspettare un millisecondo
 *     per iterazione. Ma usleep arrotonda a ms, sched_sleep arrotonda a
 *     tick, e un tick a 100Hz vale 10 ms: ogni iterazione ne costava
 *     DIECI. kbd_hw_init() poteva durare oltre 40 secondi, con il prompt
 *     gia' a video e la tastiera muta — indistinguibile da un blocco.
 *
 * Il difetto comune e' non aver mai avuto un orologio: si approssimava il
 * tempo con il conteggio (legato alla CPU) o con il sonno (legato al
 * tick). Ora c'e' SYS_UPTIME, quindi si fa la cosa diretta — si legge
 * l'ora, si cicla finche' non e' passato il tempo dichiarato, e si cede
 * la CPU nel frattempo invece di bruciarla.
 *
 * La differenza pratica rispetto alla 0.118: la scadenza e' rispettata
 * davvero, e il ritorno e' immediato appena il KBC risponde — non al
 * prossimo multiplo di 10 ms.
 *
 * L'aritmetica e' su DIFFERENZE senza segno: uptime_ms() torna a zero
 * dopo ~24,8 giorni e `ora - inizio` attraversa il wrap correttamente.
 * ============================================================================= */

/* Scadenze reali, in millisecondi. */
#define KBC_TMO_IBF      100    /* input buffer che si libera: e' veloce */
#define KBC_TMO_ACK      500    /* ACK di un comando (0xFA) */
#define KBC_TMO_CFG      200    /* lettura di un registro interno */
#define KBC_TMO_SELFTEST 1000   /* 0xAA: diagnostica interna dell'8042 */

/* =============================================================================
 * Attesa che il KBC accetti un comando (Input Buffer vuoto)
 * ============================================================================= */
static void kbc_wait_write(void)
{
    unsigned inizio = uptime_ms();
    int      veloce = KBC_POLL_FAST;

    for (;;) {
        int st = ioport_in(KBC_STATUS);
        if (st < 0 || !(st & KBC_IBF)) return;

        /* Prima un giro stretto: nel caso normale l'input buffer si
         * libera in microsecondi e non vale la pena passare dallo
         * scheduler. Solo dopo si comincia a guardare l'orologio. */
        if (veloce > 0) { veloce--; continue; }

        if (uptime_ms() - inizio >= KBC_TMO_IBF) return;
        sched_yield();
    }
}

/* Attende un byte in uscita dal KBC entro timeout_ms.
 * Ritorna il byte, o -1 se non arriva nulla entro la scadenza. */
static int kbc_wait_read_ms(unsigned timeout_ms)
{
    unsigned inizio = uptime_ms();
    int      veloce = KBC_POLL_FAST;

    for (;;) {
        int st = ioport_in(KBC_STATUS);
        if (st < 0) return -1;
        if (st & KBC_OBF) return ioport_in(KBC_DATA);

        if (veloce > 0) { veloce--; continue; }

        if (uptime_ms() - inizio >= timeout_ms) return -1;

        /* sched_yield e non usleep: cedere la CPU non impone una durata
         * minima, quindi si torna a controllare il KBC appena lo
         * scheduler ci rida' il turno. Con usleep si aspetterebbe
         * comunque un tick intero anche se il byte e' gia' arrivato. */
        sched_yield();
    }
}

/* =============================================================================
 * LED tastiera
 * ============================================================================= */
static void kbd_set_leds(unsigned char leds)
{
    kbc_wait_write();
    ioport_out(KBC_DATA, KBC_CMD_SET_LEDS);
    kbc_wait_write();
    ioport_out(KBC_DATA, leds & 0x07);
    g_leds = leds;
}

/* =============================================================================
 * kbd_process_scancode — traduzione + line discipline
 *
 * Stessa semantica del vecchio handler IRQ1, ma senza contesto
 * interrupt: qui possiamo tranquillamente fare syscall (l'eco è una
 * write) perché siamo in un normale flusso di processo.
 * ============================================================================= */
static void kbd_process_scancode(unsigned char sc)
{
    ConsoleIn *c = &g_c[g_attiva];
    char       ascii;

    /* Prefisso tasto esteso */
    if (sc == 0xE0) {
        g_e0 = 1;
        return;
    }

    /* Key release (bit 7) */
    if (sc & 0x80) {
        unsigned char key = (unsigned char)(sc & 0x7F);
        unsigned char era_e0 = g_e0;

        g_e0 = 0;
        if (key == 0x2A || key == 0x36) g_shift = 0;
        if (key == 0x1D)                g_ctrl  = 0;
        /* ! E0 38 E' AltGr, 38 DA SOLO E' Alt SINISTRO, e vanno tenuti
         * separati: Alt sinistro commuta le console, AltGr scrive le
         * graffe. Confonderli darebbe una tastiera su cui AltGr+Shift+e`
         * cambia schermo invece di aprire un blocco. */
        if (key == 0x38) { if (era_e0) g_altgr = 0; else g_alt = 0; }
        return;
    }

    /* =====================================================================
     * ! AltGr VA RICONOSCIUTO QUI, PRIMA DEI TASTI ESTESI
     *
     * AltGr e' `e0 38`, cioe' arriva con il prefisso dei tasti estesi. Il
     * blocco piu' sotto tratta quel prefisso come «tasto di movimento» e
     * lo consegna come sequenza ANSI o lo scarta: 0x38 finiva nel suo
     * `default: return` e il modificatore non veniva mai visto.
     *
     * Il sintomo era che su una tastiera italiana le parentesi quadre e
     * le graffe non si scrivevano — cioe' che AltGr non esisteva — mentre
     * il codice che lo gestiva c'era ed era giusto: stava solo dopo.
     * ===================================================================== */
    if (g_e0 && sc == 0x38) { g_altgr = 1; g_e0 = 0; return; }

    /* =====================================================================
     * Alt+F1..F4 — COMMUTAZIONE DI CONSOLE
     *
     * Prima di ogni altra cosa, e senza consegnare il tasto a nessuno.
     * Deve valere in cooked come in raw, e deve valere anche mentre un
     * programma a schermo intero ha la console: se lo lasciassimo
     * passare, basterebbe un editor che usa Alt+F per il menu File per
     * rendere impossibile cambiare schermo — cioe' proprio nel caso in
     * cui serve di piu'.
     * ===================================================================== */
    if (KBD_ALT_FN_COMMUTA && g_alt && !g_e0) {
        unsigned n = kbd_func_key(sc);
        if (n >= KBD_K_F1 && n < KBD_K_F1 + KBD_N_CONSOLE) {
            kbd_commuta(n - KBD_K_F1);
            return;
        }
    }

    /* Tasti estesi: consegnati come sequenze ANSI, esattamente come
     * faceva il TTY in-kernel. Non passano dal buffer di riga: un
     * cursore non è testo editabile. */
    if (g_e0) {
        char seq[3];
        g_e0 = 0;

        if (c->raw) { kbd_raw_scancode(sc, 1); return; }

        seq[0] = '\x1B';
        seq[1] = '[';
        switch (sc) {
            case 0x48: seq[2] = 'A'; break;   /* Up */
            case 0x50: seq[2] = 'B'; break;   /* Down */
            case 0x4D: seq[2] = 'C'; break;   /* Right */
            case 0x4B: seq[2] = 'D'; break;   /* Left */
            case 0x47: seq[2] = 'H'; break;   /* Home */
            case 0x4F: seq[2] = 'F'; break;   /* End */
            default:   return;                /* tasto esteso non mappato */
        }
        if (c->line_len + 3 < KBD_LINE_MAX) {
            c->vis[c->line_len]  = 0;
            c->line[c->line_len++] = seq[0];
            c->vis[c->line_len]  = 0;
            c->line[c->line_len++] = seq[1];
            c->vis[c->line_len]  = 0;
            c->line[c->line_len++] = seq[2];
        }
        return;
    }

    /* Modificatori */
    if (sc == 0x2A || sc == 0x36) { g_shift = 1; return; }
    if (sc == 0x1D)               { g_ctrl  = 1; return; }
    /* 0x38 senza prefisso e' l'Alt SINISTRO: commuta le console. Quello
     * di destra e' stato preso qui sopra. */
    if (sc == 0x38)               { g_alt   = 1; return; }
    if (sc == 0x3A) {
        g_caps = (unsigned char)(g_caps ^ 1);
        g_leds = (unsigned char)((g_leds & ~0x04) | (g_caps ? 0x04 : 0));
        kbd_set_leds(g_leds);
        return;
    }

    /* Da qui in giù i due modelli divergono: la modalità raw non ha una
     * riga in costruzione, quindi non ha né eco né Backspace da gestire. */
    if (c->raw) { kbd_raw_scancode(sc, 0); return; }

    if (sc >= 128) return;

    ascii = traduci(sc);

    /* CapsLock inverte il caso delle sole lettere */
    if (g_caps && ascii >= 'a' && ascii <= 'z') ascii = (char)(ascii - 32);
    else if (g_caps && ascii >= 'A' && ascii <= 'Z') ascii = (char)(ascii + 32);

    /* Ctrl+lettera → codice di controllo */
    if (g_ctrl && ascii >= 'a' && ascii <= 'z') ascii = (char)(ascii - 'a' + 1);
    else if (g_ctrl && ascii >= 'A' && ascii <= 'Z') ascii = (char)(ascii - 'A' + 1);

    if (ascii == 0) return;

    /* Backspace: agisce sul buffer di riga, che è ancora tutto qui — è
     * proprio per questo che la riga non viene consegnata carattere per
     * carattere (vedi il commento sulla line discipline in tty.c). */
    if (ascii == '\b') {
        /* Si toglie UN carattere dal fondo del buffer, e si cancella una
         * colonna solo se quel carattere era stato ecoato. */
        if (c->line_len > 0) {
            if (c->vis[--c->line_len]) {
                c->line_col--;
                echo("\b \b", 3);
            }
        }

        /* =================================================================
         * ARRIVATI AL LIMITE, LA RIGA SI AZZERA DEL TUTTO.
         *
         * Se non resta piu' niente di visibile, quello che eventualmente
         * sopravvive nel buffer sono caratteri di controllo: invisibili,
         * non cancellabili a vista, e pronti a finire dentro il comando
         * che si sta per battere. Chi cancella fino in fondo si aspetta
         * una riga vuota, e deve trovarla vuota davvero.
         * ================================================================= */
        if (c->line_col == 0) c->line_len = 0;
        return;
    }

    if (ascii == '\n' || ascii == '\r') {
        echo_char('\n');
        ring_put_line(c, c->line, c->line_len);
        c->line_len = 0;
        c->line_col = 0;
        return;
    }

    /* ! SI DECIDE PRIMA SE IL CARATTERE ENTRA, POI LO SI ECOA.
     * L'ordine inverso — ecoare e poi accorgersi che la riga e' piena —
     * lascia sullo schermo caratteri che nel buffer non ci sono: si
     * esegue meno di quello che si legge, e il Backspace non ritrova piu'
     * il conto. A riga piena il carattere si rifiuta e basta. */
    if (c->line_len >= KBD_LINE_MAX - 1) return;

    c->vis[c->line_len]    = (unsigned char)eco_visibile(ascii);
    c->line[c->line_len++] = ascii;

    if (c->vis[c->line_len - 1]) {
        c->line_col++;
        echo_char(ascii);
    }
}

/* =============================================================================
 * kbd_drain — svuota l'output buffer del KBC
 *
 * Chiamata a ogni notifica IRQ. È un LOOP, non una singola lettura: fra
 * l'istante in cui il kernel consegna la notifica IPC e quello in cui
 * questo processo viene schedulato passa tempo indefinito, e nel
 * frattempo possono essersi accumulati altri scancode. Il modello IPC
 * garantisce "c'è lavoro da fare", non "c'è esattamente un byte" — se
 * leggessimo un solo byte per notifica, e una notifica venisse scartata
 * perché la mailbox era piena (vedi ipc_notify_irq in kernel/ipc/ipc.c),
 * quel byte resterebbe in 0x60 per sempre: con OBF alto il KBC non
 * genera più fronti su IRQ1 e la tastiera si bloccherebbe del tutto.
 * ============================================================================= */
static void mouse_byte(unsigned char b);   /* definita piu' sotto */

static void kbd_drain(void)
{
    int guard;

    for (guard = 0; guard < KBD_RING_SIZE; guard++) {
        int st = ioport_in(KBC_STATUS);
        if (st < 0 || !(st & KBC_OBF)) break;

        int data = ioport_in(KBC_DATA);
        if (data < 0) break;

        /* Byte della seconda porta PS/2: e' il mouse. Fino ad agosto 2026
         * si leggeva e si buttava — andava comunque letto, o avrebbe tenuto
         * alto OBF bloccando la tastiera. Adesso si monta in pacchetti. */
        if (st & KBC_AUX) { mouse_byte((unsigned char)data); continue; }

        kbd_process_scancode((unsigned char)data);
    }
}

/* =============================================================================
 * kbd_hw_init — inizializzazione del controller
 * ============================================================================= */
/* =============================================================================
 * IL MOUSE PS/2 — montaggio dei pacchetti e stato accumulato
 *
 * Il protocollo visto dai client sta in kbd_proto.h. Qui c'e' il pezzo che
 * parla al dispositivo.
 * ========================================================================== */
static int          g_mouse_c_e   = 0;   /* ha risposto al reset? */
static unsigned char g_mp[3];            /* pacchetto in costruzione */
static unsigned int g_mp_n        = 0;   /* quanti byte ne ho */
static int          g_m_dx        = 0;   /* accumulati, azzerati leggendo */
static int          g_m_dy        = 0;
static unsigned int g_m_bottoni   = 0;
static unsigned int g_m_persi     = 0;
static unsigned int g_m_novita    = 0;   /* qualcosa e' cambiato dall'ultima lettura */
static unsigned int g_m_attesa_pid = 0;  /* chi aspetta con attendi=1 */

static void mouse_byte(unsigned char b)
{
    /* ! IL PRIMO BYTE SI RICONOSCE DAL BIT 3, CHE VALE SEMPRE 1. E' l'unico
     * modo di ritrovare il passo dopo un byte perso: senza questo controllo
     * un solo byte mancante sfaserebbe TUTTI i pacchetti successivi, e il
     * puntatore andrebbe a caso per sempre invece che per un istante. */
    if (g_mp_n == 0 && !(b & 0x08)) { g_m_persi++; return; }

    g_mp[g_mp_n++] = b;
    if (g_mp_n < 3) return;
    g_mp_n = 0;

    /* Traboccamento: il movimento non ci sta in un byte con segno. Il valore
     * non e' recuperabile, quindi si scarta il pacchetto e lo si dice. */
    if (g_mp[0] & 0xC0) { g_m_persi++; return; }

    {
        int dx = (int)g_mp[1];
        int dy = (int)g_mp[2];

        /* Il segno sta nel primo byte, non nel byte del movimento: sono
         * numeri a 9 bit spezzati in due posti. */
        if (g_mp[0] & 0x10) dx -= 256;
        if (g_mp[0] & 0x20) dy -= 256;

        g_m_dx += dx;
        /* La Y del PS/2 e' positiva verso l'ALTO. Si gira qui, una volta
         * sola, invece che in ogni client — vedi kbd_proto.h. */
        g_m_dy -= dy;
        g_m_bottoni = (unsigned int)(g_mp[0] & 0x07);
        g_m_novita  = 1;
    }
}

/* Manda un byte AL MOUSE (non al controller) e aspetta l'ACK. */
static int mouse_comando(unsigned char cmd)
{
    kbc_wait_write();
    ioport_out(KBC_CMD, KBC_CMD_AUX_SCRIVI);
    kbc_wait_write();
    ioport_out(KBC_DATA, cmd);
    return kbc_wait_read_ms(KBC_TMO_ACK);
}

/* =============================================================================
 * mouse_hw_init — accende la seconda porta e il dispositivo che ci sta dietro
 *
 * ! SI FA DOPO la configurazione della tastiera e PRIMA di irq_bind(12): il
 * bit di interrupt della seconda porta sta nello stesso configuration byte
 * della tastiera, e scriverlo due volte vorrebbe dire un read-modify-write in
 * piu' su un registro che il KBC serve lentamente.
 *
 * Rende 1 se un mouse ha risposto, 0 se non c'e'. Un mouse assente NON e' un
 * errore: moltissime macchine non ne hanno uno, e il driver deve partire
 * lo stesso — la tastiera non c'entra niente.
 * ========================================================================== */
static int mouse_hw_init(void)
{
    int r, drain;

    /* ! PRIMA SI SVUOTA IL BUFFER, e non e' prudenza generica: kbd_set_leds()
     * manda 0xED piu' il valore e NON legge i due ACK che la tastiera
     * risponde. Restano li'. Il primo comando al mouse li leggerebbe al posto
     * delle proprie risposte — e siccome un ACK vale 0xFA come quello del
     * mouse, il reset «riuscirebbe» e poi il controllo del self-test
     * troverebbe il secondo 0xFA invece di 0xAA. Sintomo: «nessun mouse ha
     * risposto» su una macchina che il mouse ce l'ha. */
    for (drain = 0; drain < 16; drain++) {
        int st = ioport_in(KBC_STATUS);
        if (st < 0 || !(st & KBC_OBF)) break;
        ioport_in(KBC_DATA);
    }

    kbc_wait_write();
    ioport_out(KBC_CMD, KBC_CMD_AUX_ENABLE);

    /* Reset. Risponde ACK, poi 0xAA (self-test superato), poi 0x00 (id del
     * dispositivo). I due byte dopo l'ACK si consumano qui: lasciarli nel
     * buffer li farebbe arrivare al montatore di pacchetti come se fossero
     * movimento. */
    r = mouse_comando(MOUSE_CMD_RESET);
    if (r != MOUSE_ACK) return 0;

    if (kbc_wait_read_ms(KBC_TMO_SELFTEST) != 0xAA) return 0;
    (void)kbc_wait_read_ms(KBC_TMO_ACK);      /* id del dispositivo */

    if (mouse_comando(MOUSE_CMD_DEFAULTS)  != MOUSE_ACK) return 0;
    if (mouse_comando(MOUSE_CMD_REPORT_ON) != MOUSE_ACK) return 0;

    return 1;
}

/* Consegna lo stato a `pid` e AZZERA l'accumulo. Vedi kbd_proto.h per il
 * perche' si somma invece di mandare un evento per movimento. */
static void mouse_rispondi(unsigned int pid)
{
    MouseStato s;

    s.dx       = g_m_dx;
    s.dy       = g_m_dy;
    s.bottoni  = g_m_bottoni;
    s.presente = (unsigned int)g_mouse_c_e;
    s.persi    = g_m_persi;

    if (ipc_send(pid, MOUSE_MSG_STATO, &s, sizeof(s)) < 0) return;

    /* ! SI AZZERA SOLO SE LA CONSEGNA E' RIUSCITA. Azzerare prima vorrebbe
     * dire buttare via uno spostamento che il client non ha mai ricevuto —
     * un puntatore che ogni tanto non si muove, e nessun modo di accorgersene
     * guardando il client. */
    g_m_dx     = 0;
    g_m_dy     = 0;
    g_m_persi  = 0;
    g_m_novita = 0;
}

/* Chi aspettava con attendi=1 viene servito quando qualcosa cambia. */
static void try_serve_mouse(void)
{
    unsigned int pid;

    if (g_m_attesa_pid == 0 || !g_m_novita) return;

    pid = g_m_attesa_pid;
    g_m_attesa_pid = 0;
    mouse_rispondi(pid);
}

static void kbd_hw_init(void)
{
    int drain;
    int result;

    /* Svuota qualunque byte residuo lasciato dal BIOS */
    for (drain = 0; drain < 32; drain++) {
        int st = ioport_in(KBC_STATUS);
        if (st < 0 || !(st & KBC_OBF)) break;
        ioport_in(KBC_DATA);
    }

    /* Self-test del controller */
    kbc_wait_write();
    ioport_out(KBC_CMD, KBC_CMD_SELF_TEST);
    result = kbc_wait_read_ms(KBC_TMO_SELFTEST);
    if (result != 0x55) {
        printf("kbd: self-test KBC fallito (0x%x), continuo\n", result);
    }

    /* Abilita l'interfaccia tastiera */
    kbc_wait_write();
    ioport_out(KBC_CMD, KBC_CMD_KBD_ENABLE);

    /* =====================================================================
     * RIPROGRAMMA IL CONFIGURATION BYTE — LA TASTIERA MORTA SU HARDWARE
     * REALE (2026-07-31)
     *
     * Sintomo: sul Pentium II il sistema si avviava fino in fondo — banner
     * del kernel, banner della shell, prompt — e poi NESSUN tasto produceva
     * alcun effetto. In QEMU la stessa immagine funzionava perfettamente.
     *
     * Causa: il self-test 0xAA qui sopra. Su un 8042 vero quel comando
     * REINIZIALIZZA il configuration byte del controller, e la
     * configurazione predefinita ha il bit 0 (KBD interrupt enable) a
     * ZERO. Da quel momento il controller riceve gli scancode ma non alza
     * mai IRQ1: nessuna notifica arriva al driver, che resta in ipc_recv()
     * per sempre. La tastiera non e' rotta e il buffer non e' pieno —
     * semplicemente nessuno viene piu' avvisato.
     *
     * Il driver non toccava affatto il configuration byte: si affidava
     * implicitamente al fatto che il BIOS avesse lasciato IRQ1 attivo. E'
     * vero prima del self-test, non dopo.
     *
     * Perche' in emulazione non si vedeva: l'8042 di QEMU gestisce 0xAA
     * restituendo 0x55 e aggiornando i flag di stato, ma NON tocca il
     * proprio registro di modo. Il bit di interrupt resta come il BIOS
     * l'aveva lasciato, e tutto sembra funzionare.
     *
     * Read-modify-write invece di scrivere un valore fisso: il byte
     * contiene anche impostazioni della seconda porta PS/2 e il system
     * flag, che non ci riguardano e non vanno calpestati.
     * ===================================================================== */
    {
        int cfg;

        kbc_wait_write();
        ioport_out(KBC_CMD, KBC_CMD_READ_CFG);
        cfg = kbc_wait_read_ms(KBC_TMO_CFG);

        if (cfg < 0) {
            printf("kbd: lettura del configuration byte fallita, "
                   "uso il ripiego 0x%x\n", KBC_CFG_FALLBACK);
            cfg = KBC_CFG_FALLBACK;
        }

        cfg |=  KBC_CFG_KBD_INT;      /* IRQ1: senza questo, tastiera muta */
        cfg &= ~KBC_CFG_KBD_CLOCK;    /* il bit ALTO significa "disabilitato" */
        cfg |=  KBC_CFG_TRANSLATE;    /* le tabelle qui sopra sono set 1 */
        /* La seconda porta, cioe' il mouse. Stesso registro: si scrive una
         * volta sola per tutti e due, che e' il motivo per cui il mouse si
         * accende qui e non in mouse_hw_init(). */
        cfg |=  KBC_CFG_AUX_INT;      /* IRQ12 */
        cfg &= ~KBC_CFG_AUX_CLOCK;    /* anche qui il bit ALTO disabilita */

        kbc_wait_write();
        ioport_out(KBC_CMD, KBC_CMD_WRITE_CFG);
        kbc_wait_write();
        ioport_out(KBC_DATA, (unsigned char)cfg);
    }

    /* Abilita la scansione (comando alla tastiera, non al controller) */
    kbc_wait_write();
    ioport_out(KBC_DATA, KBC_CMD_ENABLE_SCAN);
    result = kbc_wait_read_ms(KBC_TMO_ACK);
    if (result != 0xFA) {
        printf("kbd: ACK enable-scan non ricevuto (0x%x)\n", result);
    }

    kbd_set_leds(0x00);
}

/* =============================================================================
 * main — loop di servizio
 *
 * Un solo punto di attesa per tutto il driver: ipc_recv(). Ci arrivano
 * due cose diverse, distinguibili dal mittente:
 *   - sender_pid == IPC_SENDER_KERNEL (0) e type == IPC_TYPE_IRQ_NOTIFY
 *     → notifica hardware: c'è (probabilmente) roba da leggere in 0x60
 *   - qualunque altro mittente → richiesta di un client
 *
 * argc/argv sono ignorati: kernel_main carica i driver con elf_load
 * diretto, non via sys_spawn, quindi lo stack iniziale non contiene un
 * vero vettore di argomenti.
 * ============================================================================= */
int main(int argc, char **argv)
{
    IpcMessage    meta;
    unsigned char payload[64];
    int           rc, i;
    char          scelta[KBD_MAP_NOME_MAX];

    /* =====================================================================
     * La disposizione: prima kernel.cfg, poi la riga di comando.
     *
     *     [kernel]
     *     keymap = it
     *
     * ! SI LEGGE LA CONFIGURAZIONE PRIMA DI REGISTRARSI, cioe' prima che
     * qualcuno possa digitare. Farlo dopo darebbe una finestra — corta,
     * ma reale — in cui i primi tasti battuti vengono tradotti con la
     * disposizione sbagliata, e sono proprio i tasti dell'autoexec.
     *
     * ! UN NOME SCONOSCIUTO NON E' FATALE. Si dice e si tiene `us`: una
     * tastiera muta perche' la configurazione ha un refuso e' un sistema
     * che non si puo' usare nemmeno per correggere quel refuso.
     * ===================================================================== */
    scelta[0] = '\0';
    if (getconf("keymap", scelta, sizeof(scelta)) < 0) scelta[0] = '\0';

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            strncpy(scelta, argv[++i], sizeof(scelta) - 1);
            scelta[sizeof(scelta) - 1] = '\0';
        } else if (strcmp(argv[i], "-i") == 0) {
            /* La convenzione comune a tutti i driver: sonda, dice cosa ha
             * trovato ed esce con 0 se si applica a questa macchina.
             *
             * ! QUI LA RISPOSTA E' SEMPRE SI', E VA DETTO PERCHE'. Non
             * c'e' niente da sondare: il controllore 8042 e' parte del
             * chipset di ogni PC x86, non sta su un bus che si possa
             * interrogare, e non esiste un registro che risponda «non ci
             * sono». Fingere una sonda che puo' fallire darebbe una
             * macchina senza tastiera ogni volta che la finta sbaglia —
             * e su quella macchina non si potrebbe nemmeno correggere
             * l'errore. Un `si'` onesto e dichiarato vale di piu'. */
            if (scelta[0] != '\0') keymap_scegli(scelta);
            printf("kbd: tastiera PS/2 (controllore 8042), disposizione '%s'\n",
                   g_map->nome);
            printf("     Presente su ogni PC x86: questo driver serve sempre.\n");
            return 0;
        }
    }

    if (scelta[0] != '\0' && keymap_scegli(scelta) < 0)
        printf("kbd: disposizione '%s' sconosciuta, uso '%s'. "
               "`keymap` le elenca.\n", scelta, g_map->nome);

    rc = ipc_register(KBD_SERVICE_NAME);
    if (rc < 0) {
        printf("kbd: ipc_register('%s') fallita (%d) - esco\n",
               KBD_SERVICE_NAME, rc);
        return 1;
    }

    rc = ioport_bind(KBC_PORT_BASE, KBC_PORT_COUNT);
    if (rc < 0) {
        printf("kbd: ioport_bind(0x%x,%u) fallita (%d) - esco\n",
               KBC_PORT_BASE, KBC_PORT_COUNT, rc);
        return 1;
    }

    kbd_hw_init();

    /* irq_bind() smaschera l'IRQ nel PIC: da qui in poi gli IRQ1
     * arrivano. Va fatto DOPO kbd_hw_init(), che genera traffico sul
     * KBC (ACK del self-test, ACK di enable-scan) che non vogliamo
     * scambiare per input dell'utente. */
    /* Il mouse PS/2, sulla seconda porta dello stesso controller. Assente
     * non e' un errore: moltissime macchine non ne hanno uno, e la tastiera
     * non c'entra niente. */
    g_mouse_c_e = mouse_hw_init();
    if (g_mouse_c_e) {
        rc = irq_bind(MOUSE_IRQ);
        if (rc < 0) {
            printf("kbd: irq_bind(%u) per il mouse fallita (%d) - "
                   "vado avanti senza\n", MOUSE_IRQ, rc);
            g_mouse_c_e = 0;
        }
    }

    rc = irq_bind(KBD_IRQ);
    if (rc < 0) {
        printf("kbd: irq_bind(%u) fallita (%d) - esco\n", KBD_IRQ, rc);
        return 1;
    }

    /* Un tasto premuto fra kbd_hw_init() e irq_bind() lascia OBF alto
     * senza aver prodotto un fronte utile: senza questa drenata iniziale
     * il KBC resterebbe zittito per sempre. */
    kbd_drain();

    /* Messaggio di avvio solo se il boot è verboso: con verboseboot=0
     * questa riga sarebbe l'unica a sfuggire al silenzio, perché non
     * passa da klog() del kernel ma è una write() di un processo ring3 —
     * il filtro di livello del kernel non la vede nemmeno. I messaggi di
     * errore qui sopra restano invece sempre stampati. */
    if (verboseboot()) {
        printf("kbd: servizio '%s' pronto (IRQ%u, porte 0x%x-0x%x, PID %d)\n",
               KBD_SERVICE_NAME, KBD_IRQ, KBC_PORT_BASE,
               KBC_PORT_BASE + KBC_PORT_COUNT - 1, getpid());
    }

    for (;;) {
        if (ipc_recv(&meta, payload, sizeof(payload)) < 0) continue;

        if (meta.sender_pid == IPC_SENDER_KERNEL &&
            meta.tipo == IPC_TYPE_IRQ_NOTIFY) {
            kbd_drain();

            /* ! RIAPRIRE LA LINEA, E FARLO DOPO AVER SVUOTATO IL KBC.
             *
             * Da agosto 2026 il kernel MASCHERA l'IRQ prima di consegnare
             * la notifica (vedi kernel/arch/x86/isr.c): senza questa
             * chiamata la tastiera funzionerebbe per un tasto solo e poi
             * resterebbe muta per sempre.
             *
             * L'ordine conta. Se si riaprisse prima di kbd_drain(), il
             * byte sarebbe ancora nel buffer di uscita del KBC e su una
             * macchina dove OBF tiene alta la linea l'interrupt
             * ripartirebbe subito — cioe' esattamente la tempesta che il
             * mascheramento serve a evitare. Prima si svuota, poi si
             * riapre. */
            /* ! SI RIAPRE L'IRQ CHE E' ARRIVATO, non sempre l'1. Da quando
             * c'e' anche il mouse ne arrivano due, e riaprire sempre il
             * primo lascerebbe l'IRQ12 mascherato per sempre dopo il primo
             * movimento: il mouse funzionerebbe per un pacchetto e poi
             * basta. Il numero e' nel payload della notifica. */
            {
                unsigned int quale = KBD_IRQ;
                if (meta.len >= sizeof(unsigned int))
                    memcpy(&quale, payload, sizeof(unsigned int));
                irq_done(quale);
            }

            /* Solo la console in primo piano riceve input, quindi solo
             * lei puo' avere qualcosa di nuovo da consegnare. */
            try_serve_reader(g_attiva);
            try_serve_keyreader(g_attiva);
            try_serve_mouse();
            continue;
        }

        if (meta.tipo == KBD_MSG_SCANCODE) {
            /* Byte di un'altra sorgente — oggi la tastiera USB — trattati
             * esattamente come se venissero dall'8042. Vedi kbd_proto.h. */
            unsigned int i;

            for (i = 0; i < meta.len && i < sizeof(payload); i++)
                kbd_process_scancode(payload[i]);

            try_serve_reader(g_attiva);
            try_serve_keyreader(g_attiva);
            continue;
        }

        if (meta.tipo == MOUSE_MSG_LEGGI) {
            unsigned int attendi = 0;

            if (meta.len >= sizeof(unsigned int))
                memcpy(&attendi, payload, sizeof(unsigned int));

            /* Con attendi=0 si risponde sempre, anche se non e' successo
             * niente: e' il modo di chiedere «dove sono i bottoni adesso»
             * senza restare appesi. */
            if (!attendi || g_m_novita || !g_mouse_c_e) {
                mouse_rispondi(meta.sender_pid);
            } else {
                /* ! UN SOLO ATTENDENTE, e l'ultimo vince — come per la
                 * tastiera. Il puntatore e' uno solo; due processi che se lo
                 * contendono sono gia' un guaio a monte, e tenerne una coda
                 * qui non lo risolverebbe. */
                g_m_attesa_pid = meta.sender_pid;
            }
            continue;
        }

        if (meta.tipo == KBD_MSG_SETMODE) {
            KbdSetMode m;

            m.modo    = KBD_MODE_COOKED;
            m.console = 0;

            if (meta.len >= sizeof(unsigned)) {
                memcpy(&m.modo, payload, sizeof(unsigned));
                if (meta.len >= sizeof(KbdSetMode)) {
                    memcpy(&m.console, payload + sizeof(unsigned), sizeof(unsigned));
                }
                if (m.console >= KBD_N_CONSOLE) m.console = 0;
                kbd_set_mode(m.console, m.modo);

                /* Chi torna in cooked lascia dietro di sé un'eventuale
                 * READKEY mai soddisfatta: la si dimentica qui,
                 * altrimenti il primo tasto della sessione successiva
                 * finirebbe a un destinatario che non lo aspetta più. */
                if (!g_c[m.console].raw) g_c[m.console].keyreader_pid = 0;
            }
            continue;
        }

        /* =================================================================
         * Disposizione della tastiera
         *
         * ! SI RISPONDE SEMPRE, anche a una richiesta sbagliata. Chi
         * chiede resta fermo in ipc_recv finche' non gli si risponde, e
         * un `keymap xx` con un refuso non deve piantare il programma che
         * doveva segnalare il refuso.
         * ================================================================= */
        if (meta.tipo == KBD_MSG_SETMAP || meta.tipo == KBD_MSG_GETMAP) {
            KbdMapInfo info;
            int        i, off = 0;

            memset(&info, 0, sizeof(info));
            info.esito = 0;

            if (meta.tipo == KBD_MSG_SETMAP) {
                KbdSetMap s;

                memset(&s, 0, sizeof(s));
                if (meta.len >= sizeof(s)) memcpy(&s, payload, sizeof(s));
                s.nome[KBD_MAP_NOME_MAX - 1] = '\0';

                if (keymap_scegli(s.nome) < 0) info.esito = -EINVAL;
                else printf("kbd: disposizione '%s' (%s)\n",
                            g_map->nome, g_map->descrizione);
            }

            strncpy(info.attiva, g_map->nome, sizeof(info.attiva) - 1);
            strncpy(info.descrizione, g_map->descrizione,
                    sizeof(info.descrizione) - 1);
            info.n = (unsigned int)KEYMAP_N;

            for (i = 0; i < KEYMAP_N; i++) {
                const char *s = g_keymaps[i].nome;

                if (off > 0 && off < (int)sizeof(info.elenco) - 1)
                    info.elenco[off++] = ' ';
                while (*s && off < (int)sizeof(info.elenco) - 1)
                    info.elenco[off++] = *s++;
            }
            info.elenco[off] = '\0';

            ipc_send(meta.sender_pid, KBD_MSG_MAPINFO, &info, sizeof(info));
            continue;
        }

        if (meta.tipo == KBD_MSG_READKEY) {
            unsigned n = 0;

            if (meta.len >= sizeof(unsigned)) memcpy(&n, payload, sizeof(unsigned));
            if (n >= KBD_N_CONSOLE) n = 0;

            if (!g_c[n].raw) {
                /* In cooked non ci sono eventi da consegnare. Non si
                 * risponde: il client resterebbe comunque bloccato in
                 * ipc_recv, ma inventargli un tasto sarebbe peggio. */
                printf("kbd: READKEY da PID %u su console %u in cooked, ignorata\n",
                       meta.sender_pid, n);
                continue;
            }

            g_c[n].keyreader_pid = meta.sender_pid;

            /* Servito solo se e' la console in primo piano. Sulle altre
             * la richiesta resta pendente e verra' onorata alla
             * commutazione: e' cosi' che un editor su una console
             * nascosta se ne sta fermo invece di rubare i tasti a chi
             * sta davvero digitando. */
            if (n == g_attiva) try_serve_keyreader(n);
            continue;
        }

        if (meta.tipo == KBD_MSG_READLINE) {
            KbdReadLine r;

            r.max     = KBD_LINE_MAX;
            r.console = 0;

            if (meta.len >= sizeof(unsigned)) {
                memcpy(&r.max, payload, sizeof(unsigned));
                if (meta.len >= sizeof(KbdReadLine)) {
                    memcpy(&r.console, payload + sizeof(unsigned), sizeof(unsigned));
                }
            }
            if (r.max == 0 || r.max > KBD_LINE_MAX) r.max = KBD_LINE_MAX;
            if (r.console >= KBD_N_CONSOLE) r.console = 0;

            /* Una richiesta di riga mentre quella console e' in raw
             * significa che il programma a schermo intero non c'e' piu' —
             * e' la shell che ha ripreso il prompt. Vedi kbd_proto.h: e'
             * la seconda delle due reti di sicurezza contro una console
             * lasciata muta. */
            if (g_c[r.console].raw) {
                printf("kbd: READLINE su console %u in raw, ripristino cooked\n",
                       r.console);
                kbd_set_mode(r.console, KBD_MODE_COOKED);
                g_c[r.console].keyreader_pid = 0;
            }

            /* Un solo lettore per console. Se ne arriva un secondo, il
             * nuovo sostituisce il vecchio — che resterebbe comunque
             * bloccato in ipc_recv. Segnalato perche' e' una situazione
             * che non dovrebbe capitare: su ogni console c'e' una shell
             * sola, e legge in modo sincrono. */
            if (g_c[r.console].reader_pid != 0 &&
                g_c[r.console].reader_pid != meta.sender_pid) {
                printf("kbd: su console %u la richiesta di PID %u sostituisce "
                       "quella di PID %u\n",
                       r.console, meta.sender_pid, g_c[r.console].reader_pid);
            }
            g_c[r.console].reader_pid = meta.sender_pid;
            g_c[r.console].reader_max = r.max;

            /* Potrebbe esserci già type-ahead pronto: non aspettare un
             * altro tasto per consegnarlo. Ma solo se e' la console in
             * primo piano — sulle altre la richiesta resta in attesa. */
            if (r.console == g_attiva) try_serve_reader(r.console);
            continue;
        }

        printf("kbd: messaggio ignoto type=%u da PID %u\n",
               meta.tipo, meta.sender_pid);
    }

    /* non raggiunto */
}

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

/* IRQ della tastiera PS/2 */
#define KBD_IRQ     1

/* Le costanti di attesa stanno più sotto, insieme a kbc_wait_read_ms().
 * Qui c'era KBC_POLL_MAX=2000, un limite a CONTEGGIO DI ITERAZIONI, con
 * la motivazione "il KBC risponde in decine di microsecondi": vera per un
 * registro, falsa per il self-test 0xAA. Vedi il commento là. */

/* =============================================================================
 * Mappa scancode set 1 → ASCII (US QWERTY)
 * ============================================================================= */
static const unsigned char sc_normal[128] = {
    0,   27,  '1','2','3','4','5','6','7','8',
    '9','0', '-','=','\b','\t','q','w','e','r',
    't', 'y','u','i','o','p','[',']','\n', 0,
    'a', 's','d','f','g','h','j','k','l',';',
    '\'','`', 0, '\\','z','x','c','v','b','n',
    'm', ',','.','/', 0, '*', 0, ' ', 0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  '-',0,  0,  0,
    '+', 0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0
};

static const unsigned char sc_shift[128] = {
    0,   27,  '!','@','#','$','%','^','&','*',
    '(',')', '_','+','\b','\t','Q','W','E','R',
    'T', 'Y','U','I','O','P','{','}','\n', 0,
    'A', 'S','D','F','G','H','J','K','L',':',
    '"', '~', 0, '|','Z','X','C','V','B','N',
    'M', '<','>','?', 0, '*', 0, ' ', 0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  '-',0,  0,  0,
    '+', 0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0
};

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
static unsigned char g_caps  = 0;
static unsigned char g_e0    = 0;   /* prefisso tasto esteso 0xE0 */
static unsigned char g_leds  = 0;   /* bit0=Scroll, bit1=Num, bit2=Caps */

/* Buffer di riga in costruzione (line discipline "cooked") */
static char     g_line[KBD_LINE_MAX];
static unsigned g_line_len = 0;

/* Ring buffer delle righe già completate ma non ancora richieste da
 * nessuno (type-ahead: l'utente digita mentre il client sta ancora
 * eseguendo il comando precedente). Contiene i byte delle righe, '\n'
 * incluso; g_rlines conta quanti '\n' ci sono, cioè quante righe
 * complete sono disponibili. */
#define KBD_RING_SIZE   1024
static char     g_ring[KBD_RING_SIZE];
static unsigned g_rhead = 0, g_rtail = 0, g_rcount = 0, g_rlines = 0;

/* Client attualmente in attesa di una riga (0 = nessuno) */
static unsigned g_reader_pid = 0;
static unsigned g_reader_max = KBD_LINE_MAX;

/* =============================================================================
 * Modalità raw — vedi il commento esteso in kbd_proto.h
 *
 * Le due modalità condividono la traduzione degli scancode fino ai
 * modificatori, poi divergono del tutto: in cooked il risultato è un
 * carattere che entra in g_line (con eco e Backspace), in raw è un
 * evento che entra in g_keys così com'è.
 *
 * Il ring degli eventi è piccolo di proposito. Il type-ahead di una
 * console a righe ha senso — si digita il comando successivo mentre il
 * precedente lavora — mentre trentadue tasti accumulati davanti a un
 * editor che non li ha ancora letti sono già una raffica che l'utente
 * non ha voluto: meglio perderne la coda che ripeterla a schermo dopo
 * secondi. Serve solo a coprire la finestra fra un READKEY e il
 * successivo.
 * ============================================================================= */
#define KBD_KEYRING_SIZE    32

static unsigned char g_raw = 0;          /* 0 = cooked, 1 = raw */
static unsigned g_keys[KBD_KEYRING_SIZE];
static unsigned g_khead = 0, g_ktail = 0, g_kcount = 0;

/* Client in attesa di un tasto (0 = nessuno). Distinto da g_reader_pid:
 * sono due protocolli diversi e tenerli separati evita di consegnare una
 * riga a chi aveva chiesto un tasto. */
static unsigned g_keyreader_pid = 0;

/* =============================================================================
 * Eco a video — passa dal TTY del kernel via fd 1, non da accessi
 * diretti alla memoria VGA (che in ring3 non è mappata).
 * ============================================================================= */
static void echo(const char *s, unsigned n)
{
    write(1, s, n);
}

static void echo_char(char c)
{
    echo(&c, 1);
}

/* =============================================================================
 * Ring buffer delle righe complete
 * ============================================================================= */
static void ring_put_line(const char *s, unsigned len)
{
    unsigned i;

    /* O entra tutta la riga (terminatore compreso) o non entra affatto:
     * una riga troncata a metà resterebbe senza '\n' e g_rlines
     * mentirebbe a ring_take_line, che consumerebbe anche la riga
     * successiva credendola la stessa. Meglio perdere la riga in
     * overflow che disallineare il buffer. */
    if (len + 1 > KBD_RING_SIZE - g_rcount) return;

    for (i = 0; i < len; i++) {
        g_ring[g_rtail] = s[i];
        g_rtail = (g_rtail + 1) % KBD_RING_SIZE;
        g_rcount++;
    }
    g_ring[g_rtail] = '\n';
    g_rtail = (g_rtail + 1) % KBD_RING_SIZE;
    g_rcount++;
    g_rlines++;
}

/* Estrae la prossima riga completa. Copia al più 'max' byte in out; i
 * byte eccedenti vengono comunque consumati dal ring (la riga non resta
 * a metà). Ritorna il numero di byte copiati. */
static unsigned ring_take_line(char *out, unsigned max)
{
    unsigned n = 0;

    if (g_rlines == 0) return 0;

    while (g_rcount > 0) {
        char c = g_ring[g_rhead];
        g_rhead = (g_rhead + 1) % KBD_RING_SIZE;
        g_rcount--;
        if (n < max) out[n++] = c;
        if (c == '\n') break;
    }
    g_rlines--;
    return n;
}

/* =============================================================================
 * try_serve_reader — consegna una riga al client in attesa, se ce n'è
 * una pronta. Chiamata sia quando arriva una richiesta (potrebbe esserci
 * già type-ahead in coda) sia quando una riga si completa (il client
 * potrebbe essere già in attesa da prima).
 * ============================================================================= */
static void try_serve_reader(void)
{
    char     out[KBD_LINE_MAX];
    unsigned max = g_reader_max;
    unsigned len;

    if (g_reader_pid == 0 || g_rlines == 0) return;

    if (max > sizeof(out)) max = sizeof(out);
    len = ring_take_line(out, max);

    if (ipc_send(g_reader_pid, KBD_MSG_LINE, out, len) < 0) {
        /* Client sparito fra la richiesta e la risposta (terminato, o
         * ucciso da un fault): la riga è persa, ma il driver resta vivo
         * e pronto per il prossimo. */
        printf("kbd: consegna a PID %u fallita, client sparito\n", g_reader_pid);
    }
    g_reader_pid = 0;
}

/* =============================================================================
 * Ring degli eventi tasto (modalità raw)
 * ============================================================================= */
static void key_put(unsigned key)
{
    if (g_kcount >= KBD_KEYRING_SIZE) return;   /* raffica: si perde la coda */

    g_keys[g_ktail] = key;
    g_ktail = (g_ktail + 1) % KBD_KEYRING_SIZE;
    g_kcount++;
}

/* =============================================================================
 * try_serve_keyreader — gemello di try_serve_reader per la modalità raw.
 *
 * Il fallimento della consegna qui vale più che nel caso a righe: se il
 * programma a schermo intero è morto senza rimettere la console in
 * cooked, questo è il momento in cui ce ne accorgiamo, ed è l'ultimo
 * utile per non lasciare la tastiera muta. Vedi kbd_proto.h.
 * ============================================================================= */
static void kbd_set_mode(unsigned mode);

static void try_serve_keyreader(void)
{
    unsigned key;

    if (g_keyreader_pid == 0 || g_kcount == 0) return;

    key = g_keys[g_khead];
    g_khead = (g_khead + 1) % KBD_KEYRING_SIZE;
    g_kcount--;

    if (ipc_send(g_keyreader_pid, KBD_MSG_KEY, &key, sizeof(key)) < 0) {
        printf("kbd: consegna tasto a PID %u fallita, torno in cooked\n",
               g_keyreader_pid);
        g_keyreader_pid = 0;
        kbd_set_mode(KBD_MODE_COOKED);
        return;
    }
    g_keyreader_pid = 0;
}

/* =============================================================================
 * kbd_set_mode — passa fra riga e tasto singolo.
 *
 * Butta via lo stato di input accumulato in ENTRAMBE le direzioni: la
 * riga a metà e il type-ahead sono testo raccolto con la line discipline
 * cooked, gli eventi in coda sono tasti raccolti senza; consegnare gli
 * uni con le regole degli altri darebbe input inventato.
 * ============================================================================= */
static void kbd_set_mode(unsigned mode)
{
    unsigned char nuovo = (mode == KBD_MODE_RAW) ? 1 : 0;

    if (nuovo == g_raw) return;

    g_raw       = nuovo;
    g_line_len  = 0;
    g_rhead = g_rtail = g_rcount = g_rlines = 0;
    g_khead = g_ktail = g_kcount = 0;
    g_e0        = 0;

    /* I modificatori NON si azzerano: sono stato fisico della tastiera,
     * non input accumulato. Se l'utente tiene premuto Shift mentre il
     * programma cambia modalità, Shift è ancora premuto. */
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
    unsigned base;
    char     ascii;

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
        key_put(base | kbd_mods());
        return;
    }

    base = kbd_func_key(sc);
    if (base != 0) { key_put(base | kbd_mods()); return; }

    /* Navigazione dal tastierino (NumLock spento): sc_normal ha 0 per
     * questi codici, quindi non si sta rubando nessun carattere. */
    base = kbd_nav_key(sc);
    if (base != 0) { key_put(base | kbd_mods()); return; }

    if (sc >= 128) return;

    ascii = (char)(g_shift ? sc_shift[sc] : sc_normal[sc]);
    if (g_caps && ascii >= 'a' && ascii <= 'z')      ascii = (char)(ascii - 32);
    else if (g_caps && ascii >= 'A' && ascii <= 'Z') ascii = (char)(ascii + 32);

    if (ascii == 0) return;

    /* Nessuna trasformazione Ctrl: il modificatore viaggia a parte. */
    key_put((unsigned)(unsigned char)ascii | kbd_mods());
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
    char ascii;

    /* Prefisso tasto esteso */
    if (sc == 0xE0) {
        g_e0 = 1;
        return;
    }

    /* Key release (bit 7) */
    if (sc & 0x80) {
        unsigned char key = (unsigned char)(sc & 0x7F);
        g_e0 = 0;
        if (key == 0x2A || key == 0x36) g_shift = 0;
        if (key == 0x1D)                g_ctrl  = 0;
        if (key == 0x38)                g_alt   = 0;
        return;
    }

    /* Tasti estesi: consegnati come sequenze ANSI, esattamente come
     * faceva il TTY in-kernel. Non passano dal buffer di riga: un
     * cursore non è testo editabile. */
    if (g_e0) {
        char seq[3];
        g_e0 = 0;

        if (g_raw) { kbd_raw_scancode(sc, 1); return; }

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
        if (g_line_len + 3 < KBD_LINE_MAX) {
            g_line[g_line_len++] = seq[0];
            g_line[g_line_len++] = seq[1];
            g_line[g_line_len++] = seq[2];
        }
        return;
    }

    /* Modificatori */
    if (sc == 0x2A || sc == 0x36) { g_shift = 1; return; }
    if (sc == 0x1D)               { g_ctrl  = 1; return; }
    if (sc == 0x38)               { g_alt   = 1; return; }
    if (sc == 0x3A) {
        g_caps = (unsigned char)(g_caps ^ 1);
        g_leds = (unsigned char)((g_leds & ~0x04) | (g_caps ? 0x04 : 0));
        kbd_set_leds(g_leds);
        return;
    }

    /* Da qui in giù i due modelli divergono: la modalità raw non ha una
     * riga in costruzione, quindi non ha né eco né Backspace da gestire. */
    if (g_raw) { kbd_raw_scancode(sc, 0); return; }

    if (sc >= 128) return;

    ascii = (char)(g_shift ? sc_shift[sc] : sc_normal[sc]);

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
        if (g_line_len > 0) {
            g_line_len--;
            echo("\b \b", 3);
        }
        return;
    }

    if (ascii == '\n' || ascii == '\r') {
        echo_char('\n');
        ring_put_line(g_line, g_line_len);
        g_line_len = 0;
        return;
    }

    /* Eco solo dei caratteri stampabili.
     *
     * I caratteri di controllo (ESC, Ctrl+lettera, ...) finiscono
     * regolarmente nel buffer di riga — un programma che li aspetta deve
     * riceverli — ma NON vengono ecoati: la VGA li renderebbe come glifi
     * casuali della code page 437, sporcando lo schermo. Il caso concreto
     * è ESC, che /bin/textline usa per annullare una riga: senza questo
     * filtro l'utente vedrebbe comparire una freccia al posto di niente.
     * Il tab resta ecoato perché a video ha un effetto sensato. */
    if ((unsigned char)ascii >= 32 || ascii == '\t') {
        echo_char(ascii);
    }

    if (g_line_len < KBD_LINE_MAX - 1) {
        g_line[g_line_len++] = ascii;
    }
    /* Riga piena: il carattere è già stato ecoato ma non viene
     * accumulato. Limite ereditato dal TTY in-kernel, non una
     * regressione di questa migrazione. */
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
static void kbd_drain(void)
{
    int guard;

    for (guard = 0; guard < KBD_RING_SIZE; guard++) {
        int st = ioport_in(KBC_STATUS);
        if (st < 0 || !(st & KBC_OBF)) break;

        int data = ioport_in(KBC_DATA);
        if (data < 0) break;

        /* Byte proveniente dalla seconda porta PS/2 (mouse): non è
         * roba nostra, ma va comunque letto — l'abbiamo appena fatto —
         * altrimenti resterebbe a tenere alto OBF. Scartato. */
        if (st & KBC_AUX) continue;

        kbd_process_scancode((unsigned char)data);
    }
}

/* =============================================================================
 * kbd_hw_init — inizializzazione del controller
 * ============================================================================= */
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
    int           rc;

    (void)argc;
    (void)argv;

    rc = ipc_register(KBD_SERVICE_NAME);
    if (rc < 0) {
        printf("kbd: ipc_register('%s') fallita (%d) — esco\n",
               KBD_SERVICE_NAME, rc);
        return 1;
    }

    rc = ioport_bind(KBC_PORT_BASE, KBC_PORT_COUNT);
    if (rc < 0) {
        printf("kbd: ioport_bind(0x%x,%u) fallita (%d) — esco\n",
               KBC_PORT_BASE, KBC_PORT_COUNT, rc);
        return 1;
    }

    kbd_hw_init();

    /* irq_bind() smaschera l'IRQ nel PIC: da qui in poi gli IRQ1
     * arrivano. Va fatto DOPO kbd_hw_init(), che genera traffico sul
     * KBC (ACK del self-test, ACK di enable-scan) che non vogliamo
     * scambiare per input dell'utente. */
    rc = irq_bind(KBD_IRQ);
    if (rc < 0) {
        printf("kbd: irq_bind(%u) fallita (%d) — esco\n", KBD_IRQ, rc);
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
            meta.type == IPC_TYPE_IRQ_NOTIFY) {
            kbd_drain();
            try_serve_reader();
            try_serve_keyreader();
            continue;
        }

        if (meta.type == KBD_MSG_SETMODE) {
            unsigned mode = KBD_MODE_COOKED;
            if (meta.len >= sizeof(unsigned)) {
                memcpy(&mode, payload, sizeof(unsigned));
                kbd_set_mode(mode);
            }
            /* Chi torna in cooked lascia dietro di sé un'eventuale
             * READKEY mai soddisfatta: la si dimentica qui, altrimenti
             * il primo tasto della sessione successiva finirebbe a un
             * destinatario che non lo aspetta più. */
            if (!g_raw) g_keyreader_pid = 0;
            continue;
        }

        if (meta.type == KBD_MSG_READKEY) {
            if (!g_raw) {
                /* In cooked non ci sono eventi da consegnare. Non si
                 * risponde: il client resterebbe comunque bloccato in
                 * ipc_recv, ma inventargli un tasto sarebbe peggio. */
                printf("kbd: READKEY da PID %u in modalita' cooked, ignorata\n",
                       meta.sender_pid);
                continue;
            }
            g_keyreader_pid = meta.sender_pid;
            try_serve_keyreader();   /* potrebbe esserci già un tasto pronto */
            continue;
        }

        if (meta.type == KBD_MSG_READLINE) {
            unsigned max = KBD_LINE_MAX;

            /* Una richiesta di riga mentre siamo in raw significa che il
             * programma a schermo intero non c'è più — è la shell che
             * ha ripreso il prompt. Vedi kbd_proto.h: è la seconda delle
             * due reti di sicurezza contro una console lasciata muta. */
            if (g_raw) {
                printf("kbd: READLINE in modalita' raw, ripristino cooked\n");
                kbd_set_mode(KBD_MODE_COOKED);
                g_keyreader_pid = 0;
            }

            if (meta.len >= sizeof(unsigned)) {
                memcpy(&max, payload, sizeof(unsigned));
            }
            if (max == 0 || max > KBD_LINE_MAX) max = KBD_LINE_MAX;

            /* Un solo lettore alla volta: la console è una sola. Se
             * arriva una richiesta mentre un'altra è pendente, la nuova
             * sostituisce la vecchia — il client precedente resterebbe
             * comunque bloccato in ipc_recv, ma è una situazione che
             * oggi non si verifica (solo il TTY del kernel chiede righe,
             * e lo fa in modo sincrono per conto di un processo alla
             * volta). Segnalata perché diventerà reale il giorno in cui
             * ci saranno più terminali. */
            if (g_reader_pid != 0 && g_reader_pid != meta.sender_pid) {
                printf("kbd: richiesta da PID %u sostituisce quella di PID %u\n",
                       meta.sender_pid, g_reader_pid);
            }
            g_reader_pid = meta.sender_pid;
            g_reader_max = max;

            /* Potrebbe esserci già type-ahead pronto: non aspettare un
             * altro tasto per consegnarlo. */
            try_serve_reader();
            continue;
        }

        printf("kbd: messaggio ignoto type=%u da PID %u\n",
               meta.type, meta.sender_pid);
    }

    /* non raggiunto */
}

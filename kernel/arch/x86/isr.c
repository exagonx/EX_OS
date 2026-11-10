/* =============================================================================
 * kernel/arch/x86/isr.c
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
#include "entropia.h"
#include "ipc.h"
#include "syscall.h"
#include "tsc.h"
#include "smp.h"
#include "kmalloc.h"
#include "sched.h"

/* =============================================================================
 * Nomi delle eccezioni CPU (per messaggi di errore)
 * ============================================================================= */
static const char *exception_names[32] = {
    "#DE Division Error",               /*  0 */
    "#DB Debug",                        /*  1 */
    "NMI Non-Maskable Interrupt",       /*  2 */
    "#BP Breakpoint",                   /*  3 */
    "#OF Overflow",                     /*  4 */
    "#BR Bound Range Exceeded",         /*  5 */
    "#UD Invalid Opcode",               /*  6 */
    "#NM Device Not Available",         /*  7 */
    "#DF Double Fault",                 /*  8 */
    "Coprocessor Segment Overrun",      /*  9 */
    "#TS Invalid TSS",                  /* 10 */
    "#NP Segment Not Present",          /* 11 */
    "#SS Stack-Segment Fault",          /* 12 */
    "#GP General Protection Fault",     /* 13 */
    "#PF Page Fault",                   /* 14 */
    "Reserved",                         /* 15 */
    "#MF x87 FP Error",                 /* 16 */
    "#AC Alignment Check",              /* 17 */
    "#MC Machine Check",                /* 18 */
    "#XF SIMD FP Exception",            /* 19 */
    "#VE Virtualization Exception",     /* 20 */
    "#CP Control Protection",           /* 21 */
    "Reserved",                         /* 22 */
    "Reserved",                         /* 23 */
    "Reserved",                         /* 24 */
    "Reserved",                         /* 25 */
    "Reserved",                         /* 26 */
    "Reserved",                         /* 27 */
    "Reserved",                         /* 28 */
    "#HV Hypervisor Injection",         /* 29 */
    "#VC VMM Communication",            /* 30 */
    "#SX Security Exception",           /* 31 */
};

/* =============================================================================
 * Tabelle di handler registrabili
 * ============================================================================= */
static isr_handler_fn exception_handlers[32] = { NULL };
static isr_handler_fn irq_handlers[16]       = { NULL };

/* =============================================================================
 * Tabella IRQ -> PID dei driver ring3
 *
 * Un driver ring3 rivendica un IRQ hardware con irq_bind_process(). Da
 * quel momento, se non c'è un handler kernel-space registrato per quel
 * IRQ (caso normale per hardware gestito interamente in userspace, come
 * tastiera o floppy convertiti a driver ring3), il dispatcher consegna
 * una notifica IPC ai PID proprietari invece di ignorare l'interrupt.
 * 0 = slot libero (i PID validi partono da 1).
 *
 * -----------------------------------------------------------------------------
 * ! PIU' DI UN PROPRIETARIO, dal 1 settembre 2026, E NON E' UN LUSSO
 *
 * Fino a quel giorno era un PID solo, con accanto scritto «evita che due
 * driver si contendano lo stesso hardware». La frase era giusta per l'ISA,
 * dove una linea appartiene a una scheda e basta. SUL PCI E' FALSA: le quattro
 * linee INTA-INTD di uno slot si distribuiscono a rotazione fra gli slot, e
 * una macchina con più di quattro dispositivi ne ha per forza due sulla
 * stessa. Non è un caso raro: è la configurazione normale.
 *
 * ! E SI E' VISTO SUBITO. Il primo driver audio PCI provato in QEMU trovava
 * la scheda, leggeva i BAR, e moriva su `irq_bind(11) fallita (-17)`: l'IRQ 11
 * ce l'aveva la scheda di rete, avviata trenta secondi prima da /boot/avvio.sh.
 * Con un proprietario solo, «hai la rete» e «hai il suono» erano alternative.
 *
 * COME SI SERVE UNA LINEA CONDIVISA. Il dispositivo non dice di essere lui:
 * lo si scopre chiedendo a ognuno. Il kernel maschera la linea e notifica
 * TUTTI i proprietari; ognuno guarda il registro di stato della propria
 * scheda e o la serve o dice «non era mio». La linea si riapre quando hanno
 * risposto tutti — vedi irq_pendenti qui sotto.
 *
 * ! LA RIAPERTURA VUOLE L'ULTIMO, NON IL PRIMO. Riaprire alla prima risposta
 * rimetterebbe la linea in gioco mentre l'altro driver non ha ancora azzerato
 * la SUA scheda: la linea è ancora alta, e si torna esattamente alla tempesta
 * che il mascheramento esiste per evitare. Per questo c'è una maschera di chi
 * deve ancora rispondere, e non un contatore.
 * ============================================================================= */
#define IRQ_PROPRIETARI_MAX 4

static uint32_t irq_owner_pid[16][IRQ_PROPRIETARI_MAX];

/* Bit i = il proprietario nello slot i non ha ancora chiamato irq_done(). */
static uint32_t irq_pendenti[16] = { 0 };

/* ! QUANTE NOTIFICHE SONO STATE CONSEGNATE DAVVERO, per linea. Non e' una
 * statistica: e' la misura che separa «l'hardware non interrompe» da «il
 * driver non ascolta», e senza di lei le due cose si somigliano troppo. Un
 * driver vivo, registrato, che possiede la linea e ha ricevuto ZERO notifiche
 * mentre qualcuno batteva sui tasti dice tutto quello che c'e' da dire. */
static uint32_t irq_notifiche[16] = { 0 };

uint32_t irq_notifiche_di(uint8_t irq)
{
    return (irq < 16) ? irq_notifiche[irq] : 0;
}

/* C'è qualcuno su questa linea? Solo una scansione di quattro parole.
 *
 * ! NON SI CHIAMA irq_ripulisci() DAL DISPATCHER, e non è un dettaglio di
 * velocità: quella scorre la tabella dei processi con proc_get_by_pid, cioè
 * fa un ciclo dentro un ciclo DENTRO L'INTERRUPT, a ogni battuta di tasto. Il
 * lavoro di ripulitura è raro per natura — un driver muore una volta — e sta
 * dove è raro anche chiamarlo: bind, done, unbind, e la morte del processo.
 * Un proprietario morto qui non fa danno: ipc_notify_irq lo riconosce da sé e
 * rende 0, quindi non gli si aspetta nessuna risposta. */
static int irq_qualcuno(uint8_t irq)
{
    int i;

    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++)
        if (irq_owner_pid[irq][i] != 0) return 1;
    return 0;
}

isr_handler_fn irq_handler_get(uint8_t irq)
{
    return (irq < 16) ? irq_handlers[irq] : 0;
}

uint32_t irq_proprietario(uint8_t irq)
{
    int i;

    if (irq >= 16) return 0;
    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++)
        if (irq_owner_pid[irq][i] != 0) return irq_owner_pid[irq][i];
    return 0;
}

/* Quanti proprietari VIVI ha questa linea, e intanto ripulisce gli slot di
 * chi è morto: un processo terminato non deve tenere chiusa una linea per
 * sempre, e nessuno chiamerà irq_done() per lui. */
/* The lines in edge mode (0.323): see IRQ EDGES WITH THEIR INSTANT below. */
typedef struct {
    uint32_t  pid;          /* 0 = the mode is off */
    uint64_t *anello;       /* cap instants (kmalloc) */
    uint32_t  cap;
    uint32_t  testa;        /* next to write */
    uint32_t  coda;         /* next to read */
    uint32_t  persi;        /* edges that found the ring full */
    uint32_t  avvisato;     /* a notification is on its way */
    uint32_t  fronti;       /* edges seen since the mode was turned on */
    uint32_t  spuri;        /* of which taken from a "spurious" IRQ 7 or 15 */
} IrqFronti;

static IrqFronti g_fronti[16];
static void fronti_spegni(uint8_t irq);

static uint32_t irq_ripulisci(uint8_t irq)
{
    uint32_t vivi = 0;
    int      i;

    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++) {
        Process *p;

        if (irq_owner_pid[irq][i] == 0) continue;

        p = proc_get_by_pid(irq_owner_pid[irq][i]);
        if (p == NULL || p->state == PROC_UNUSED || p->state == PROC_ZOMBIE) {
            irq_owner_pid[irq][i] = 0;
            irq_pendenti[irq] &= ~(1u << i);
            continue;
        }
        vivi++;
    }
    return vivi;
}

/* =============================================================================
 * irq_bind_process — un driver ring3 rivendica un IRQ hardware
 *
 * Ritorna 0 su successo, <0 se il IRQ è già rivendicato da un altro
 * processo vivo (evita che due driver si contendano lo stesso hardware).
 * Sblocca l'IRQ nel PIC: da questo momento gli interrupt arrivano.
 * ============================================================================= */
int32_t irq_bind_process(uint8_t irq, uint32_t pid)
{
    int i, libero = -1;

    if (irq >= 16) return ERR(EINVAL);

    irq_ripulisci(irq);

    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++) {
        /* ! LA STESSA LINEA RICHIESTA DUE VOLTE DALLO STESSO PROCESSO NON E'
         * UN CONFLITTO. Senza questo, un driver che sonda le linee per
         * scoprire la propria e poi rivendica quella trovata riceve -EEXIST da
         * se' stesso, e il messaggio che ne esce — «IRQ gia' rivendicato» —
         * manda a cercare un altro driver che non c'e'. */
        if (irq_owner_pid[irq][i] == pid) {
            pic_unmask_irq(irq);
            return 0;
        }
        if (irq_owner_pid[irq][i] == 0 && libero < 0) libero = i;
    }

    if (libero < 0) {
        klog(LOG_WARN, "IRQ%u: gia' %d proprietari, non ce ne stanno altri",
             irq, IRQ_PROPRIETARI_MAX);
        return ERR(EEXIST);
    }

    irq_owner_pid[irq][libero] = pid;

    /* ! IL BIT DI ATTESA DELLO SLOT SI AZZERA QUI. Uno slot riusato puo'
     * portarsi dietro il «deve ancora rispondere» del proprietario di prima:
     * un bit acceso che nessuno spegnera' mai tiene la linea chiusa per
     * sempre, e il sintomo — l'hardware muto, senza un errore — non
     * assomiglia per niente a un bit rimasto acceso. */
    irq_pendenti[irq] &= ~(1u << libero);

    pic_unmask_irq(irq);
    return 0;
}

/* =============================================================================
 * irq_done_process — il driver ha servito l'interrupt, si può riaprire
 *
 * Contropartita del mascheramento in irq_dispatch(): finché il driver non
 * chiama questa, la linea resta chiusa. Il perché sta nel commento lungo
 * là sotto, e si riassume in una riga — un driver ring3 non può azzerare
 * il registro di stato della scheda mentre l'interrupt è in corso, perché
 * in quel momento non sta girando.
 *
 * Si verifica il proprietario: senza il controllo, un processo qualunque
 * potrebbe riaprire un IRQ che un altro driver sta ancora servendo, cioè
 * riaccendere la tempesta che il mascheramento serve a evitare.
 * ============================================================================= */
int32_t irq_done_process(uint8_t irq, uint32_t pid)
{
    int i, mio = -1;

    if (irq >= 16) return ERR(EINVAL);

    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++)
        if (irq_owner_pid[irq][i] == pid) { mio = i; break; }

    if (mio < 0) return ERR(EPERM);

    irq_pendenti[irq] &= ~(1u << mio);

    /* Un proprietario morto fra la notifica e adesso non deve tenere chiusa
     * la linea: irq_ripulisci gli toglie anche il bit di attesa. */
    irq_ripulisci(irq);

    /* ! SI RIAPRE QUANDO HANNO RISPOSTO TUTTI, non al primo. Il perche' sta
     * accanto a irq_pendenti, in cima al file: riaprire mentre un altro
     * driver non ha ancora azzerato la propria scheda rimette in gioco una
     * linea che e' ancora alta. */
    if (irq_pendenti[irq] == 0) pic_unmask_irq(irq);
    return 0;
}

/* =============================================================================
 * irq_unbind_uno — un driver rilascia UNA linea che aveva rivendicato
 *
 * ! ESISTE PER LA SONDA DELL'AUDIO ISA, ed e' l'unico caso che la chiede.
 * Una Sound Blaster anteriore alla 16 non dice su quale IRQ e' cablata: il
 * ponticello lo ha messo qualcuno vent'anni fa e non c'e' un registro che
 * lo riporti. L'unico modo di scoprirlo e' provocare un interrupt e
 * guardare quale linea si alza — cioe' rivendicare i quattro candidati
 * (5, 7, 10, 2/9), suonare un blocco di silenzio, e vedere quale notifica
 * arriva.
 *
 * Senza questa, dopo la sonda il driver terrebbe per sempre le tre linee
 * SBAGLIATE: l'IRQ 7 e' la parallela, il 10 e' libero per una scheda
 * qualunque, e un driver futuro se li troverebbe occupati da un processo
 * che non li usa e non sa di averli. Il claim dev'essere restituibile
 * perche' la sonda lo prende PER SCOPRIRE, non per usare.
 *
 * Rilascia solo cio' che e' proprio: -EPERM se la linea e' di un altro,
 * per la stessa ragione per cui la verifica c'e' in irq_done_process.
 * ============================================================================= */
int32_t irq_unbind_uno(uint8_t irq, uint32_t pid)
{
    int i, mio = -1;

    if (irq >= 16) return ERR(EINVAL);

    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++)
        if (irq_owner_pid[irq][i] == pid) { mio = i; break; }

    if (mio < 0) return ERR(EPERM);

    irq_owner_pid[irq][mio] = 0;
    irq_pendenti[irq] &= ~(1u << mio);
    if (g_fronti[irq].pid == pid) fronti_spegni(irq);

    /* ! LA LINEA SI CHIUDE SOLO SE NON LA VUOLE PIU' NESSUNO. Chiuderla
     * sempre — com'era quando i proprietari erano uno solo — vorrebbe dire
     * che un driver che restituisce una linea presa in prestito per una sonda
     * spegne l'hardware di qualcun altro. */
    if (irq_ripulisci(irq) == 0) pic_mask_irq(irq);
    else if (irq_pendenti[irq] == 0) pic_unmask_irq(irq);
    return 0;
}


/* =============================================================================
 * IRQ EDGES WITH THEIR INSTANT (kernel 0.323, voce 98)
 *
 * ! WHEN AN EDGE CAME IS KNOWN ONLY HERE. A ring 3 driver hears of its
 * interrupt when it is scheduled: a millisecond later, or ten, never the same.
 * For a printer that is nothing. For the parallel port it is the whole job: a
 * Commodore datasette sends a square wave on ACK whose PERIOD is the data
 * (about 180 to 500 microseconds a wave, mtap/ptap as documentation), and a
 * prototype board on the same pin wants to know when each of its signals
 * arrived. So the stub notes the fine clock at the entry, BEFORE waiting for
 * the kernel's lock (with two processors the other may hold it for a while),
 * and irq_handler stores it in a ring that the driver reads when it can.
 *
 * ! AND THE LINE IS NOT MASKED IN THIS MODE. The masking below exists for
 * level-triggered lines that stay high until the driver clears its card; the
 * ACK interrupt of a parallel port is an EDGE and has nothing to clear.
 * Masked until irq_done, every wave after the first would be lost while the
 * driver waits to be scheduled. That is why the mode is only for a line with
 * ONE owner: a shared PCI line is level-triggered by nature.
 * ============================================================================= */

/* The fine clock at the entry of the last interrupt, one slot per processor
 * (grown by irq_posti from posti_cresci, like the other per-processor tables). */
static uint64_t  g_istante_1[1];
static uint64_t *g_istante = g_istante_1;
static uint32_t  g_istante_posti = 1;

int irq_posti(uint32_t posti)
{
    uint64_t *nuovo;
    uint32_t  i;
    uintptr_t flag;

    if (posti <= g_istante_posti) return 0;
    nuovo = (uint64_t *)kmalloc(posti * sizeof(uint64_t));
    if (nuovo == NULL) return -1;
    for (i = 0; i < posti; i++) nuovo[i] = 0;
    flag = read_eflags();
    interrupts_disable();
    nuovo[0]         = g_istante[0];
    g_istante        = nuovo;
    g_istante_posti  = posti;
    if (flag & 0x200u) interrupts_enable();
    return 0;
}

/* The finest clock this machine has: the TSC, or on a 486 (no RDTSC) the
 * ticks and what channel 0 of the PIT has counted down since the last one
 * (divisor 11931, 100 Hz, set in entry.asm). */
#define PIT_DIV_TICK 11931u

uint64_t orologio_fine(void)
{
    uint32_t conta, t;

    if (tsc_c_e()) return tsc_leggi();
    port_outb(0x43, 0x00);                  /* latch channel 0 */
    conta  = port_inb(0x40);
    conta |= (uint32_t)port_inb(0x40) << 8;
    t = g_ticks;
    if (conta > PIT_DIV_TICK) conta = PIT_DIV_TICK;
    return (uint64_t)t * PIT_DIV_TICK + (PIT_DIV_TICK - conta);
}

uint32_t orologio_fine_hz(void)
{
    return tsc_c_e() ? tsc_hz() : 1193182u;
}

/* Called by the stubs before bkl_entra, interrupts off: nothing shared. */
void irq_istante(void)
{
    uint32_t n = cpu_n();

    if (n < g_istante_posti) g_istante[n] = orologio_fine();
}


static void fronti_spegni(uint8_t irq)
{
    uint64_t *a = g_fronti[irq].anello;
    uintptr_t flag = read_eflags();

    interrupts_disable();
    g_fronti[irq].pid = 0;
    g_fronti[irq].anello = NULL;
    g_fronti[irq].cap = 0;
    if (flag & 0x200u) interrupts_enable();
    if (a) kfree(a);
}

/* From irq_handler: 1 if the edge was taken here (and nothing else to do). */
static int fronti_prendi(uint8_t irq, uint64_t quando)
{
    IrqFronti *f = &g_fronti[irq];
    uint32_t   dopo;

    if (f->pid == 0) return 0;
    f->fronti++;
    dopo = f->testa + 1;
    if (dopo == f->cap) dopo = 0;
    if (dopo == f->coda) f->persi++;
    else { f->anello[f->testa] = quando; f->testa = dopo; }
    if (!f->avvisato && ipc_notify_irq(f->pid, irq)) f->avvisato = 1;
    return 1;
}

/* ! A "SPURIOUS" IRQ 7 ON A LINE IN EDGE MODE IS AN EDGE. The 8259 gives
 * IRQ 7 (15 on the slave) with nothing in service when the request went
 * down before the processor took it: the line DID go up. With two
 * processors the one the PIC talks to may be waiting for the kernel's lock
 * with interrupts off, and a parallel port keeps its line up only while nACK
 * is low: half a wave of a datasette, or less. In QEMU, at 32 bit with two
 * processors, 6 edges of 300 were lost so (9 November 2026). Counted apart
 * in `spuri`, so a driver knows. */
static void spurio_fronte(uint8_t irq)
{
    if (g_fronti[irq].pid == 0) return;
    g_fronti[irq].spuri++;
    fronti_prendi(irq, g_istante[cpu_n() < g_istante_posti ? cpu_n() : 0]);
}

static int irq_mio(uint8_t irq, uint32_t pid, int *altri)
{
    int i, mio = 0;

    *altri = 0;
    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++) {
        if (irq_owner_pid[irq][i] == pid) mio = 1;
        else if (irq_owner_pid[irq][i] != 0) (*altri)++;
    }
    return mio;
}

/* SYS_IRQ_FRONTI (266): see kernel/include/syscall.h. */
int32_t irq_fronti(uint32_t op, uint8_t irq, uintptr_t a, uint32_t b, uint32_t pid)
{
    IrqFronti *f;
    int        altri;

    if (irq >= 16) return ERR(EINVAL);
    f = &g_fronti[irq];
    if (op == IRQF_STATO) {
        /* ! FOR EVERYBODY: the clock is what a driver needs to make a wave
         * at given instants too, without owning a line. The ring's numbers
         * only to its owner. */
        IrqFrontiStato *s = (IrqFrontiStato *)a;
        int mio = (f->pid == pid);

        if (!syscall_verify_ptr(s, sizeof(*s))) return ERR(EFAULT);
        s->hz      = orologio_fine_hz();
        s->tsc     = tsc_c_e() ? 1u : 0u;
        s->cap     = mio ? f->cap : 0;
        s->persi   = mio ? f->persi : 0;
        s->fronti  = mio ? f->fronti : 0;
        s->spuri   = mio ? f->spuri : 0;
        s->in_coda = mio ? (f->testa + f->cap - f->coda) % f->cap : 0;
        s->adesso  = orologio_fine();
        return 0;
    }
    irq_ripulisci(irq);
    if (!irq_mio(irq, pid, &altri)) return ERR(EPERM);

    if (op == IRQF_SPEGNI) {
        if (f->pid == pid) fronti_spegni(irq);
        return 0;
    }
    if (op == IRQF_ACCENDI) {
        uint64_t *anello;
        uintptr_t flag;
        uint32_t  cap = (uint32_t)a;

        /* ! ONLY ALONE ON THE LINE: see the head of this part. */
        if (altri) return ERR(EBUSY);
        /* the size is the driver's; the bound only keeps cap * 8 in 32 bits */
        if (cap < 2 || cap > (1u << 26)) return ERR(EINVAL);
        anello = (uint64_t *)kmalloc(cap * sizeof(uint64_t));
        if (anello == NULL) return ERR(ENOMEM);
        if (f->pid) fronti_spegni(irq);
        flag = read_eflags();
        interrupts_disable();
        f->anello = anello; f->cap = cap;
        f->testa = f->coda = 0; f->persi = 0; f->avvisato = 0; f->fronti = 0; f->spuri = 0;
        f->pid = pid;
        if (flag & 0x200u) interrupts_enable();
        pic_unmask_irq(irq);
        return 0;
    }
    if (f->pid != pid) return ERR(EINVAL);

    if (op == IRQF_LEGGI) {
        uint64_t *fuori = (uint64_t *)a;
        uint32_t  n = 0;
        uintptr_t flag;

        if (b == 0) return 0;
        if (!syscall_verify_ptr(fuori, b * sizeof(uint64_t))) return ERR(EFAULT);
        flag = read_eflags();
        interrupts_disable();
        while (n < b && f->coda != f->testa) {
            fuori[n++] = f->anello[f->coda];
            if (++f->coda == f->cap) f->coda = 0;
        }
        /* a later edge notifies again */
        f->avvisato = 0;
        if (flag & 0x200u) interrupts_enable();
        return (int32_t)n;
    }
    return ERR(EINVAL);
}

/* irq_unbind_process — rilascia il claim (chiamata alla terminazione) */
void irq_unbind_process(uint32_t pid)
{
    /* ! PID 0 IS NOBODY, and here it would be everybody: a free owner slot
     * holds 0, so every line would "belong" to it and IRQ 0-7 would be
     * masked, the timer with them (0.288, see proc_reap_zombie). */
    if (pid == 0) return;

    for (int i = 0; i < 16; i++) {
        int j, trovato = 0;

        if (g_fronti[i].pid == pid) fronti_spegni((uint8_t)i);
        for (j = 0; j < IRQ_PROPRIETARI_MAX; j++) {
            if (irq_owner_pid[i][j] != pid) continue;
            irq_owner_pid[i][j] = 0;
            irq_pendenti[i] &= ~(1u << j);
            trovato = 1;
        }
        if (!trovato) continue;

        /* ! SI CHIUDE SOLO SE ERA L'ULTIMO. Un driver che muore mentre un
         * altro condivide la sua linea non deve portarsi via anche
         * l'hardware del vicino — ed e' esattamente cio' che succedeva
         * quando i proprietari erano uno solo e questa funzione mascherava
         * sempre. */
        if (irq_ripulisci((uint8_t)i) == 0) pic_mask_irq((uint8_t)i);
        else if (irq_pendenti[i] == 0) pic_unmask_irq((uint8_t)i);
    }
}

/* =============================================================================
 * isr_register_handler — Registra handler custom per un'eccezione CPU
 * num: numero eccezione (0-31)
 * ============================================================================= */
void isr_register_handler(uint8_t num, isr_handler_fn handler)
{
    if (num < 32) {
        exception_handlers[num] = handler;
        klog(LOG_DEBUG, "ISR: handler registrato per vettore %d", num);
    }
}

/* =============================================================================
 * irq_register_handler — Registra handler per un IRQ hardware
 * irq: numero IRQ (0-15)
 * ============================================================================= */
void irq_register_handler(uint8_t irq, isr_handler_fn handler)
{
    if (irq < 16) {
        irq_handlers[irq] = handler;
        klog(LOG_DEBUG, "IRQ: handler registrato per IRQ%d", irq);
    }
}

/* =============================================================================
 * dump_registers — Stampa il contenuto di tutti i registri al momento
 * dell'eccezione. Fondamentale per il debug.
 * ============================================================================= */
static void dump_registers(InterruptFrame *f)
{
    kprintf("\n--- DUMP REGISTRI ---\n");
#if defined(__x86_64__)
    /* @EXOS-64: sedici registri di otto byte. kprintf scrive 32 bit alla
     * volta, quindi ognuno e' in due meta'. */
#define R64(v)  (uint32_t)((uint64_t)(v) >> 32), (uint32_t)(v)
    kprintf("  RAX=%08x%08x  RBX=%08x%08x  RCX=%08x%08x\n", R64(f->rax), R64(f->rbx), R64(f->rcx));
    kprintf("  RDX=%08x%08x  RSI=%08x%08x  RDI=%08x%08x\n", R64(f->rdx), R64(f->rsi), R64(f->rdi));
    kprintf("  RBP=%08x%08x  R8 =%08x%08x  R9 =%08x%08x\n", R64(f->rbp), R64(f->r8),  R64(f->r9));
    kprintf("  R10=%08x%08x  R11=%08x%08x  R12=%08x%08x\n", R64(f->r10), R64(f->r11), R64(f->r12));
    kprintf("  R13=%08x%08x  R14=%08x%08x  R15=%08x%08x\n", R64(f->r13), R64(f->r14), R64(f->r15));
    kprintf("  RIP=%08x%08x  CS =0x%04x  RFLAGS=0x%08x\n", R64(f->rip), (uint32_t)f->cs, (uint32_t)f->rflags);
    kprintf("  RSP=%08x%08x  SS =0x%04x  ERR=0x%08x  INT=%d\n", R64(f->user_rsp), (uint32_t)f->user_ss,
            (uint32_t)f->err_code, (uint32_t)f->int_no);
    if (f->int_no == 14) {
        vaddr_t cr2 = read_cr2();

        kprintf("  CR2=%08x%08x  (indirizzo page fault)\n", R64(cr2));
        kprintf("  PF flags: %s | %s | %s\n",
                (f->err_code & 0x1) ? "protection" : "not-present",
                (f->err_code & 0x2) ? "write" : "read",
                (f->err_code & 0x4) ? "user" : "supervisor");
    }
    if (f->int_no == 13 && f->err_code != 0) {
        kprintf("  GP selector: 0x%04x (TI=%d, RPL=%d)\n",
                (uint32_t)(f->err_code & 0xFFFC), (int)((f->err_code >> 2) & 1),
                (int)(f->err_code & 0x3));
    }
#undef R64
#else
    kprintf("  EAX=0x%08x  EBX=0x%08x  ECX=0x%08x  EDX=0x%08x\n",
            f->eax, f->ebx, f->ecx, f->edx);
    kprintf("  ESI=0x%08x  EDI=0x%08x  EBP=0x%08x\n",
            f->esi, f->edi, f->ebp);
    kprintf("  EIP=0x%08x  CS =0x%08x  EFLAGS=0x%08x\n",
            f->eip, f->cs, f->eflags);
    kprintf("  DS =0x%08x  ERR=0x%08x  INT=%d\n",
            f->ds, f->err_code, f->int_no);

    /* Se venivamo da ring 3, stampa anche user_esp e user_ss */
    if ((f->cs & 0x3) == 3) {
        kprintf("  USP=0x%08x  USS=0x%08x  (ring 3)\n",
                f->user_esp, f->user_ss);
    }

    /* Per Page Fault: stampa CR2 (indirizzo che ha causato il fault) */
    if (f->int_no == 14) {
        uint32_t cr2 = read_cr2();
        kprintf("  CR2=0x%08x  (indirizzo page fault)\n", cr2);

        /* Decodifica error code del page fault */
        kprintf("  PF flags: %s | %s | %s\n",
                (f->err_code & 0x1) ? "protection" : "not-present",
                (f->err_code & 0x2) ? "write" : "read",
                (f->err_code & 0x4) ? "user" : "supervisor");
    }

    /* Per #GP: decodifica error code */
    if (f->int_no == 13 && f->err_code != 0) {
        kprintf("  GP selector: 0x%04x (TI=%d, RPL=%d)\n",
                f->err_code & 0xFFFC,
                (f->err_code >> 2) & 1,
                f->err_code & 0x3);
    }
#endif
    kprintf("---------------------\n");
}

/* =============================================================================
 * isr_handler — Dispatcher principale per eccezioni CPU (0-31)
 * Chiamato da isr_common_stub in isr_stubs.asm
 * ============================================================================= */
void isr_handler(InterruptFrame *frame)
{
    uint32_t int_no = frame->int_no;

    /* Se c'è un handler registrato, usa quello */
    if (int_no < 32 && exception_handlers[int_no] != NULL) {
        exception_handlers[int_no](frame);
        return;
    }

    /* Handler di default: stampa info e kpanic per eccezioni fatali */
    const char *name = (int_no < 32) ? exception_names[int_no] : "Unknown";

    /* Eccezioni non fatali gestibili */
    if (int_no == 3) {
        /* Breakpoint: utile per debug */
        klog(LOG_DEBUG, "INT3 Breakpoint a EIP=0x%x", (uint32_t)FR_IP(frame));
        return;
    }

    /* =========================================================================
     * Eccezione proveniente da RING3: termina il processo, non il kernel.
     *
     * Il CS salvato nel frame ha i due bit bassi uguali al CPL al momento
     * dell'eccezione: 3 = il codice girava in userspace.
     *
     * BUG CORRETTO (luglio 2026): prima QUALUNQUE eccezione senza handler
     * registrato portava a kernel panic, anche se generata da un processo
     * utente. Bastava che un programma ring3 eseguisse un'istruzione
     * privilegiata per abbattere tutto il sistema — ed era esattamente
     * ciò che accadeva con i comandi `halt` e `reboot` della shell, che
     * eseguivano `cli` e `outb` in ring3: #GP (vettore 13) e panic. Il
     * sintomo riportato era "halt mostra un kernel panic".
     *
     * L'equivalente per il #PF era già stato sistemato a giugno con lo
     * stesso ragionamento (page_fault_handler in kernel/mm/paging.c): un
     * processo che sbaglia deve morire da solo. Qui la protezione viene
     * estesa a tutte le altre eccezioni — #GP, #UD, divisione per zero.
     * Il kpanic resta per le eccezioni originate in ring0, dove indicano
     * un bug reale del kernel e proseguire sarebbe pericoloso.
     * ========================================================================= */
    if ((frame->cs & 0x3) == 3) {
        Process *p = proc_get_current();
        int      sig, codice;

        /* The signal each exception is on Linux, with its si_code. A caught
         * one runs its handler instead of dying (kernel/sched/segnali.c). */
        switch (int_no) {
            case 0:  sig = 8;  codice = 1;    break;  /* SIGFPE FPE_INTDIV */
            case 16:
            case 19: sig = 8;  codice = 0;    break;  /* SIGFPE */
            case 1:
            case 3:  sig = 5;  codice = 1;    break;  /* SIGTRAP TRAP_BRKPT */
            case 6:  sig = 4;  codice = 2;    break;  /* SIGILL ILL_ILLOPN */
            case 11:
            case 12:
            case 17: sig = 7;  codice = 0x80; break;  /* SIGBUS SI_KERNEL */
            default: sig = 11; codice = 0x80; break;  /* SIGSEGV SI_KERNEL */
        }
        if (segnali_fault(frame, sig, codice, 0)) return;

        vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        kprintf("\n[FAULT] PID %u '%s': eccezione %u (%s) a EIP=0x%08x "
                "err=0x%08x - processo terminato\n",
                p ? p->pid : 0, p ? p->name : "?", int_no, name,
                (uint32_t)FR_IP(frame), (uint32_t)frame->err_code);
        vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

        klog(LOG_ERROR, "ISR: PID %u '%s' terminato da eccezione %u (%s) "
             "EIP=0x%08x err=0x%08x",
             p ? p->pid : 0, p ? p->name : "?", int_no, name,
             (uint32_t)FR_IP(frame), (uint32_t)frame->err_code);

        /* proc_exit() non ritorna: fa il context switch verso il prossimo
         * processo pronto da qui stesso. */
        proc_esci_fatale(-4);   /* convenzione stile SIGILL/SIGSEGV */
        /* mai raggiunto */
    }

    /* Eccezione in ring0 senza handler: KERNEL PANIC */
    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
    kprintf("\n");
    kprintf("+==============================================+\n");
    kprintf("|           KERNEL PANIC - ECCEZIONE CPU       |\n");
    kprintf("+==============================================+\n");
    kprintf("|  Vettore : %3d                               |\n", int_no);
    kprintf("|  Nome    : %-32s  |\n", name);
    kprintf("|  EIP     : 0x%08x                    |\n", (uint32_t)FR_IP(frame));
    kprintf("|  Errore  : 0x%08x                    |\n", (uint32_t)frame->err_code);
    kprintf("+==============================================+\n");

    dump_registers(frame);

    kprintf("\nSistema bloccato. Riavvia il computer.\n");

    /* Disabilita interrupt e halt */
    interrupts_disable();
    for(;;) {
        __asm__ volatile ("hlt");
    }
}

/* =============================================================================
 * irq_handler — Dispatcher per IRQ hardware (vettori 32-47)
 * Chiamato da irq_common_stub in isr_stubs.asm
 * ============================================================================= */
void irq_handler(InterruptFrame *frame)
{
    uint8_t irq = (uint8_t)(frame->int_no - 32);

    /* ! PRIMA DI TUTTO IL RESTO, e non e' una preferenza estetica: quello
     * che vale come entropia e' l'ISTANTE in cui l'interrupt e' arrivato,
     * e ogni riga eseguita prima di leggerlo aggiunge un ritardo
     * prevedibile che lo appiattisce. La funzione ignora da sola il timer
     * — vedi kernel/arch/x86/entropia.c. */
    entropia_evento(irq);

    /* Gestisci IRQ spurio dal master PIC (IRQ7) */
    if (irq == 7) {
        /* FIX BUG #6: la sequenza corretta è prima inviare il comando OCW3
         * "leggi ISR" (0x0B) alla porta command, POI leggere il risultato
         * dalla stessa porta. L'ordine precedente era invertito: si leggeva
         * prima di inviare il comando, ottenendo un valore casuale (IRR o
         * stato precedente) invece dell'ISR effettivo. */
        port_outb(0x20, 0x0B);          /* OCW3: comando "leggi ISR" */
        uint8_t isr = port_inb(0x20);   /* Leggi In-Service Register */
        if (!(isr & 0x80)) {
            /* IRQ7 spurio: ignora (non mandare EOI) */
            spurio_fronte(7);
            return;
        }
    }

    /* Gestisci IRQ spurio dallo slave PIC (IRQ15) */
    if (irq == 15) {
        port_outb(0xA0, 0x0B);          /* OCW3 allo slave */
        uint8_t isr = port_inb(0xA0);   /* Leggi ISR slave */
        if (!(isr & 0x80)) {
            /* IRQ15 spurio: manda EOI solo al master */
            port_outb(0x20, 0x20);
            spurio_fronte(15);
            return;
        }
    }

    /* Invia End Of Interrupt al PIC PRIMA di eseguire l'handler.
     *
     * DEADLOCK RISOLTO (luglio 2026): l'EOI era inviato DOPO l'handler.
     * L'handler di IRQ0 (sched_irq0_handler) può però chiamare
     * sched_switch_to() -> context_switch(), che cambia lo stack kernel e
     * NON ritorna: riprende l'esecuzione di un altro processo. La riga
     * pic_send_eoi(irq) restava così non eseguita fino a quando il
     * processo preemptato non veniva rischedulato — cioè fino al tick
     * successivo, che il PIC però non consegnava più perché IRQ0 era
     * ancora marcato In-Service (ISR bit 0 = 1 blocca IRQ0 e tutti gli
     * IRQ di priorità inferiore).
     *
     * Sintomo: qualunque codice che attendesse l'avanzare di g_ticks dopo
     * un context switch si bloccava per sempre. Il caso tipico era
     * `hello` (o qualunque programma) lanciato dalla shell:
     *   IRQ0 -> switch alla shell (EOI mai inviato)
     *   -> shell: spawn() -> elf_load() -> fat12_read_sector()
     *   -> fdc_motor_on() -> fdc_delay_ms(300) -> hlt in attesa di g_ticks
     *   -> nessun IRQ0 possibile -> sistema congelato (irr=03 isr=01).
     *
     * L'EOI qui è sicuro: gli IRQ entrano da interrupt gate (IF=0 per
     * tutta la durata dell'handler, vedi irq_common_stub), quindi non è
     * possibile un rientro dello stesso IRQ prima dell'iret finale. */
    pic_send_eoi(irq);

    /* An edge with its instant (0.323): taken, notified, the line stays open. */
    if (irq < 16 && irq_handlers[irq] == NULL &&
        fronti_prendi(irq, g_istante[cpu_n() < g_istante_posti ? cpu_n() : 0]))
        return;

    /* Chiama handler registrato se presente, altrimenti notifica il
     * driver ring3 che ha rivendicato questo IRQ (se esiste) */
    if (irq < 16 && irq_handlers[irq] != NULL) {
        irq_handlers[irq](frame);
    } else if (irq < 16 && irq_qualcuno((uint8_t)irq)) {
        /* =====================================================================
         * ! SI MASCHERA PRIMA DI NOTIFICARE, E SENZA QUESTO IL PCI BLOCCA
         * LA MACCHINA.
         *
         * Un driver ring3 non gira dentro l'interrupt: riceve un messaggio
         * e verrà schedulato più tardi. Fra il nostro `iret` e il momento
         * in cui quel processo tocca davvero la scheda passano dei tick.
         *
         * Su un IRQ A FRONTE (la tastiera PS/2, IRQ1) non è un problema:
         * ogni byte produce un fronte, il PIC lo latcha una volta sola e
         * basta. Su un IRQ A LIVELLO — e tutti gli interrupt PCI lo sono —
         * il dispositivo tiene la linea alta finché non gli si azzera il
         * registro di stato. L'EOI qui sopra riabilita subito il PIC, la
         * linea è ancora alta, e l'interrupt riparte immediatamente dopo
         * l'iret. Il risultato non è "qualche interrupt di troppo": è che
         * il processo driver non riceve MAI la CPU per andare ad azzerare
         * quel registro, quindi la tempesta non finisce da sola. Sistema
         * fermo, senza panic e senza niente da leggere.
         *
         * Mascherare qui e riaprire su richiesta del driver (SYS_IRQ_DONE)
         * è la stessa disciplina degli "interrupt in un thread": la linea
         * resta chiusa esattamente per il tempo in cui nessuno può ancora
         * servirla.
         *
         * Vale anche per la tastiera, che ora deve chiamare irq_done(). Una
         * regola sola per tutti i driver è meglio di due modi di
         * rivendicare un IRQ di cui uno è quello sbagliato di default —
         * e un tasto premuto mentre l'IRQ è mascherato non si perde: il
         * fronte resta nell'IRR del PIC e viene consegnato alla riapertura.
         * ===================================================================== */
        pic_mask_irq((uint8_t)irq);

        /* ! SI NOTIFICANO TUTTI, e la linea si riapre quando hanno risposto
         * tutti. Su una linea PCI condivisa il dispositivo non dice di essere
         * lui: lo si scopre chiedendo a ognuno di guardare il proprio registro
         * di stato. Il perche' per esteso sta accanto a irq_owner_pid. */
        {
            int k;
            irq_pendenti[irq] = 0;
            for (k = 0; k < IRQ_PROPRIETARI_MAX; k++) {
                if (irq_owner_pid[irq][k] == 0) continue;
                if (ipc_notify_irq(irq_owner_pid[irq][k], (uint8_t)irq)) {
                    irq_pendenti[irq] |= (1u << k);
                    irq_notifiche[irq]++;
                }
            }

            /* ! LA LINEA NON SI RIAPRE MAI DA QUI, NEMMENO SE LA NOTIFICA
             * E' CADUTA, e questa riga e' costata un blocco su hardware vero.
             *
             * C'era scritto il contrario: se nessuno ha ricevuto la notifica —
             * cassetta piena, quattro messaggi e basta — «tanto vale
             * riaprire, al massimo l'interrupt si ripresenta». Si ripresenta
             * ECCOME. Il PIC ha gia' avuto l'EOI, il controller di tastiera
             * tiene la linea alta finche' qualcuno non gli legge il byte, e
             * chi deve leggerlo e' un processo ring3 che per farlo ha bisogno
             * della CPU. Riaprire qui gliela toglie: subito dopo l'iret
             * l'interrupt riparte, trova la cassetta ancora piena, riapre, e
             * ricomincia. E' una tempesta che non finisce, ed e' esattamente
             * quella che il mascheramento esiste per evitare.
             *
             * In QEMU non si vede: la macchina e' abbastanza veloce che la
             * cassetta non si riempie mai. Su un Pentium II basta tenere
             * premuto un tasto mentre l'avvio finisce — il prompt compare e
             * poi la tastiera e' morta, con la macchina che sembra viva.
             *
             * Cosa succede adesso, che e' quello che succedeva prima di
             * questa modifica: la linea resta chiusa, e la riapre il driver
             * alla prossima irq_done() — che chiamera' di sicuro, perche' in
             * cassetta le notifiche precedenti ce le ha. Un messaggio perso
             * costa un giro di servizio in piu', non la macchina. */
        }
    } else {
        /* IRQ non gestito: log a livello debug (non panic) */
        klog(LOG_DEBUG, "IRQ%d non gestito (vettore %d)", irq, (uint32_t)frame->int_no);
    }
}

/* =============================================================================
 * isr_install — Inizializza il sistema ISR
 * Chiamata da kernel_main() dopo idt_install()
 * ============================================================================= */
void isr_install(void)
{
    uint8_t i;

    /* Azzera tutte le tabelle di handler */
    for (i = 0; i < 32; i++) exception_handlers[i] = NULL;
    for (i = 0; i < 16; i++) irq_handlers[i] = NULL;

    /* Maschera tutti gli IRQ (verranno abilitati dai driver) */
    port_outb(0x21, 0xFF);  /* Master PIC: maschera tutto */
    port_outb(0xA1, 0xFF);  /* Slave PIC: maschera tutto */

    klog(LOG_INFO, "ISR: handler installati, tutti gli IRQ mascherati");
}

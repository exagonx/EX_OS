/* =============================================================================
 * kernel/arch/x86_64/idt64.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * GLI INTERRUPT A 64 BIT (@EXOS-64, tappa 3 — 10 ottobre 2026)
 *
 * La tavola (256 voci da 16 byte: a 64 bit l'indirizzo di un gestore non sta
 * piu' in 8), il PIC rimesso dove lo mette anche il kernel a 32 bit (IRQ 0-7
 * sui vettori 32-39, 8-15 su 40-47), l'orologio, e il punto unico in cui
 * arrivano tutti: isr64_gestisci.
 *
 * ! UN'ECCEZIONE CHE NESSUNO GESTISCE FERMA IL KERNEL E DICE TUTTO. Il nome,
 * dove (RIP), con che codice, tutti i registri, e per un errore di pagina
 * l'indirizzo toccato (CR2). E' la schermata da cui si riparte quando
 * qualcosa va storto, e a 64 bit all'inizio va storto spesso: una riga
 * «errore» senza numeri vorrebbe dire rifare la prova con una stampa in piu'.
 *
 * ! IL DOPPIO ERRORE HA UNA PILA SUA (IST 1 nel TSS, vedi gdt64.c).
 * ============================================================================= */
#include "k64.h"

typedef struct PACKED {
    uint16_t basso;
    uint16_t selettore;
    uint8_t  ist;               /* 0 = la pila corrente; 1..7 = una di riserva */
    uint8_t  tipo;              /* 0x8E: presente, ring 0, porta di interrupt */
    uint16_t medio;
    uint32_t alto;
    uint32_t zero;
} Voce64;

typedef struct PACKED { uint16_t limite; uint64_t base; } Punt64;

extern uint64_t isr64_tavola[256];

static Voce64             g_idt[256];
static Gestore64          g_gestori[256];
static volatile uint64_t  g_battiti = 0;

static const char *const NOMI[32] = {
    "divisione per zero", "passo singolo", "NMI", "punto di arresto", "trabocco",
    "fuori dai limiti", "istruzione non valida", "coprocessore assente",
    "DOPPIO ERRORE", "coprocessore", "TSS non valido", "segmento assente",
    "errore di pila", "protezione generale", "errore di pagina", "(15)",
    "errore x87", "allineamento", "controllo macchina", "errore SIMD",
    "virtualizzazione", "protezione del controllo", "(22)", "(23)", "(24)", "(25)",
    "(26)", "(27)", "(28)", "(29)", "sicurezza", "(31)"
};

static void porta(uint8_t n, uint8_t tipo, uint8_t ist)
{
    uint64_t a = isr64_tavola[n];

    g_idt[n].basso     = (uint16_t)a;
    g_idt[n].selettore = SEL_K_CODICE;
    g_idt[n].ist       = ist;
    g_idt[n].tipo      = tipo;
    g_idt[n].medio     = (uint16_t)(a >> 16);
    g_idt[n].alto      = (uint32_t)(a >> 32);
    g_idt[n].zero      = 0;
}

static void pic_rimappa(void)
{
    k64_outb(0x20, 0x11); k64_outb(0xA0, 0x11);     /* si ricomincia */
    k64_outb(0x21, 0x20); k64_outb(0xA1, 0x28);     /* i vettori: 32 e 40 */
    k64_outb(0x21, 0x04); k64_outb(0xA1, 0x02);     /* il secondo sta sull'IRQ 2 */
    k64_outb(0x21, 0x01); k64_outb(0xA1, 0x01);     /* modo 8086 */
    k64_outb(0x21, 0xFF); k64_outb(0xA1, 0xFF);     /* tutti mascherati */
}

void idt64_installa(void)
{
    Punt64 p;
    int    i;

    for (i = 0; i < 256; i++) porta((uint8_t)i, 0x8E, 0);
    porta(8, 0x8E, 1);                      /* il doppio errore, sulla sua pila */
    porta(0x80, 0xEE, 0);                   /* le chiamate di sistema: anche da ring 3 */

    pic_rimappa();

    p.limite = sizeof(g_idt) - 1;
    p.base   = (uint64_t)g_idt;
    __asm__ volatile("lidt %0" : : "m"(p));
}

void idt64_gestore(uint8_t vettore, Gestore64 g) { g_gestori[vettore] = g; }

uint64_t idt64_battiti(void) { return g_battiti; }

void idt64_orologio_avvia(uint32_t hz)
{
    uint32_t div = 1193182u / hz;

    k64_outb(0x43, 0x36);                   /* canale 0, onda quadra */
    k64_outb(0x40, (uint8_t)div);
    k64_outb(0x40, (uint8_t)(div >> 8));
    k64_outb(0x21, k64_inb(0x21) & (uint8_t)~1u);   /* si smaschera l'IRQ 0 */
}

static void riga(const char *n, uint64_t v) { k64_scrivi(n); k64_esa(v); }

static void fermo(InterruptFrame *f)
{
    uint64_t cr2;

    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    k64_scrivi("\n*** ECCEZIONE ");
    k64_numero(f->int_no);
    k64_scrivi(": ");
    k64_scrivi(f->int_no < 32 ? NOMI[f->int_no] : "?");
    k64_scrivi(" ***\n");
    riga("  RIP ", f->rip);  riga("  CS ", f->cs);   riga("  RFLAGS ", f->rflags); k64_scrivi("\n");
    riga("  RSP ", f->user_rsp); riga("  SS ", f->user_ss); riga("  ERR ", f->err_code); k64_scrivi("\n");
    if (f->int_no == 14) { riga("  CR2 ", cr2); k64_scrivi("  (l'indirizzo toccato)\n"); }
    riga("  RAX ", f->rax); riga("  RBX ", f->rbx); riga("  RCX ", f->rcx); k64_scrivi("\n");
    riga("  RDX ", f->rdx); riga("  RSI ", f->rsi); riga("  RDI ", f->rdi); k64_scrivi("\n");
    riga("  RBP ", f->rbp); riga("  R8  ", f->r8);  riga("  R9  ", f->r9);  k64_scrivi("\n");
    riga("  R10 ", f->r10); riga("  R11 ", f->r11); riga("  R12 ", f->r12); k64_scrivi("\n");
    riga("  R13 ", f->r13); riga("  R14 ", f->r14); riga("  R15 ", f->r15); k64_scrivi("\n");
    k64_scrivi("Il kernel si ferma qui.\n");
    for (;;) __asm__ volatile("cli; hlt");
}

/* Qui arrivano tutti, da isr64.asm. */
#ifdef K64_INTERO
/* -----------------------------------------------------------------------------
 * IL KERNEL INTERO: gli interrupt vanno al codice COMUNE (kernel/arch/x86/
 * isr.c e syscall.c), lo stesso del kernel a 32 bit. Qui resta solo lo
 * smistamento che a 32 bit fanno i tre stub di isr_stubs.asm.
 * --------------------------------------------------------------------------- */
void isr_handler(InterruptFrame *frame);
void irq_handler(InterruptFrame *frame);
void syscall_handler(InterruptFrame *frame);

void isr64_gestisci(InterruptFrame *f)
{
    uint64_t n = f->int_no;

    if (n < 32)                 isr_handler(f);
    else if (n < 48)            irq_handler(f);     /* il fine-interrupt lo manda lui */
    else if (n == 0x80)         syscall_handler(f);
}

void idt_install(void) { idt64_installa(); }

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) k64_outb(0xA0, 0x20);
    k64_outb(0x20, 0x20);
}

void pic_mask_irq(uint8_t irq)
{
    uint16_t porta_pic = (irq < 8) ? 0x21 : 0xA1;

    k64_outb(porta_pic, k64_inb(porta_pic) | (uint8_t)(1u << (irq & 7)));
}

void pic_unmask_irq(uint8_t irq)
{
    uint16_t porta_pic = (irq < 8) ? 0x21 : 0xA1;

    k64_outb(porta_pic, k64_inb(porta_pic) & (uint8_t)~(1u << (irq & 7)));
    /* una linea del secondo PIC arriva solo se la cascata (IRQ 2) e' aperta */
    if (irq >= 8) k64_outb(0x21, k64_inb(0x21) & (uint8_t)~(1u << 2));
}
#else
void isr64_gestisci(InterruptFrame *f)
{
    uint64_t n = f->int_no;

    if (n < 256 && g_gestori[n] != 0 && g_gestori[n](f)) goto fine;

    if (n < 32) fermo(f);

    if (n == 32) g_battiti++;               /* l'orologio */

fine:
    /* La conferma al PIC: senza, quell'interrupt non arriva piu'. Al secondo
     * PIC per gli IRQ da 8 in su, e al primo sempre. */
    if (n >= 32 && n < 48) {
        if (n >= 40) k64_outb(0xA0, 0x20);
        k64_outb(0x20, 0x20);
    }
}
#endif /* K64_INTERO */

/* =============================================================================
 * kernel/kernel_main.c
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
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "fpu.h"
#include "pmm.h"
#include "paging.h"
#include "kmalloc.h"
#include "sched.h"
#include "syscall.h"
#include "fat12.h"
#include "ata.h"
#include "atapi.h"
#include "blk.h"
#include "vol.h"
#include "fat.h"
#include "vfs.h"
#include "cfg.h"
#include "elf.h"
#include "drvmgr.h"
#include "dynlink.h"
#include "tty.h"
#include "kbd_proto.h"
#include "version.h"

/* Simboli dal linker script */
extern uint32_t _kernel_start;
extern uint32_t _kernel_end;

/* Dichiarazione klog_set_level (da kprintf.c) */
void klog_set_level(int level);

/* Confronto di stringhe locale: il kernel non ha una libreria di stringhe
 * condivisa e ogni file che ne ha bisogno definisce il proprio helper
 * statico (vedi cfg_strcmp in kernel/fs/cfg.c, str_copy in sched.c).
 * Ritorna 1 se uguali. */
static int str_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* =============================================================================
 * print_boot_banner — Stampa il banner di avvio del kernel
 * ============================================================================= */
/* Banner iniziale.
 *
 * Usa g_os_version (kernel/version.c), che è composta a compile time da
 * letterali: non richiede heap, filesystem né formattazione a runtime, ed
 * è quindi stampabile qui, dove nulla di tutto ciò esiste ancora.
 *
 * NON può invece consultare /boot/kernel.cfg — né per il nome né per
 * verboseboot — e non è una dimenticanza: questa funzione gira come
 * primissima cosa in kernel_main, mentre la configurazione è caricata al
 * PASSO 13b, dopo FAT12 (PASSO 13), che a sua volta richiede paginazione,
 * heap e interrupt. Non esiste ancora un filesystem da cui leggere.
 *
 * Conseguenza pratica per verboseboot=0: le righe stampate da qui e dai
 * PASSI 1-13 vengono emesse comunque. È il PASSO 13c a ripulire lo
 * schermo una volta letta la configurazione — vedi lì. */
static void print_boot_banner(void)
{
    vga_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("  ================================================\n");
    kprintf("%s\n", g_os_version);
    kprintf("  ================================================\n");
    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kprintf("\n");
}

/* =============================================================================
 * verify_bootinfo — Verifica la struttura BootInfo passata da Stage 2
 * ============================================================================= */
static void verify_bootinfo(BootInfo *info)
{
    klog(LOG_INFO, "Verifica BootInfo da Stage 2...");

    if (info == NULL) {
        kpanic("BootInfo pointer e' NULL!");
    }

    if (info->magic != BOOTINFO_MAGIC) {
        kpanic("BootInfo magic non valido: 0x%08x (atteso 0x%08x)",
               info->magic, BOOTINFO_MAGIC);
    }

    klog(LOG_INFO, "BootInfo verificata (magic OK: 0x%08x)", info->magic);
}

/* =============================================================================
 * print_system_info — Stampa informazioni sul sistema rilevate da Stage 2
 * ============================================================================= */
static void print_system_info(BootInfo *info)
{
    uint32_t kernel_size = (uint32_t)&_kernel_end - (uint32_t)&_kernel_start;
    uint32_t total_ram   = info->mem_lower + info->mem_upper;

    klog(LOG_INFO, "--- Informazioni Sistema ---");
    klog(LOG_INFO, "  Drive di boot : 0x%02x", info->boot_drive);
    klog(LOG_INFO, "  RAM convenz.  : %u KB", info->mem_lower);
    klog(LOG_INFO, "  RAM estesa    : %u KB (%u MB)",
         info->mem_upper, info->mem_upper / 1024);
    klog(LOG_INFO, "  RAM totale    : %u KB (%u MB)",
         total_ram, total_ram / 1024);
    klog(LOG_INFO, "  Entry E820    : %u", info->e820_count);
    klog(LOG_INFO, "  Kernel base   : 0x%08x", (uint32_t)&_kernel_start);
    klog(LOG_INFO, "  Kernel end    : 0x%08x", (uint32_t)&_kernel_end);
    klog(LOG_INFO, "  Kernel size   : %u byte (%u KB)",
         kernel_size, kernel_size / 1024);

    /* Stampa mappa E820 se il livello di debug è sufficiente */
    if (info->e820_count > 0 && info->e820_addr != 0) {
        E820Entry *map = (E820Entry *)info->e820_addr;
        uint32_t   i;

        klog(LOG_DEBUG, "  Mappa E820:");
        for (i = 0; i < info->e820_count && i < 32; i++) {
            const char *type_str;
            switch (map[i].type) {
                case E820_TYPE_USABLE:       type_str = "Usabile";    break;
                case E820_TYPE_RESERVED:     type_str = "Riservato";  break;
                case E820_TYPE_ACPI_RECLAIM: type_str = "ACPI Recl."; break;
                case E820_TYPE_ACPI_NVS:     type_str = "ACPI NVS";   break;
                case E820_TYPE_BAD:          type_str = "Danneg.";    break;
                default:                     type_str = "Sconosciuto";break;
            }
            klog(LOG_DEBUG, "    [%2u] base=0x%08x len=0x%08x %s",
                 i, map[i].base_low, map[i].length_low, type_str);
        }
    }

    klog(LOG_INFO, "----------------------------");
}

/* =============================================================================
 * kernel_main — Entry point C del kernel
 * Riceve BootInfo* passata da Stage 2 tramite stack/entry.asm
 * ============================================================================= */
void kernel_main(BootInfo *info)
{
    /* Checkpoint 3: kernel_main raggiunta */
/* =========================================================================
     * PASSO 1: VGA init
     * Prima cosa assoluta: output video. Senza questo non possiamo
     * stampare nulla e i bug sono invisibili.
     * ========================================================================= */
    vga_init();
vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* =========================================================================
     * PASSO 2: Banner
     * ========================================================================= */
    print_boot_banner();
klog(LOG_INFO, "Kernel avviato in Protected Mode 32-bit");
/* =========================================================================
     * PASSO 3: Verifica BootInfo
     * Se Stage 2 non ha passato dati validi, panic immediato.
     * ========================================================================= */
    verify_bootinfo(info);
/* =========================================================================
     * PASSO 4: Informazioni sistema
     * ========================================================================= */
    print_system_info(info);
/* =========================================================================
     * PASSO 5: GDT definitiva
     * Sostituisce la GDT minimale installata da Stage 2.
     * Installa: null, kernel code/data, user code/data, TSS.
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 5] Installazione GDT...");
    gdt_install();
klog(LOG_INFO, "[PASSO 5] GDT OK");

    /* =========================================================================
     * PASSO 6: IDT — Interrupt Descriptor Table
     * Rimappa PIC, installa 256 gate, prepara handler per eccezioni e IRQ.
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 6] Installazione IDT...");
    idt_install();
klog(LOG_INFO, "[PASSO 6] IDT OK");

    /* =========================================================================
     * PASSO 7: ISR — Interrupt Service Routines
     * Inizializza tabella handler, maschera tutti gli IRQ.
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 7] Installazione ISR...");
    isr_install();
klog(LOG_INFO, "[PASSO 7] ISR OK");

    /* =========================================================================
     * PASSO 7b: FPU — coprocessore matematico x87
     *
     * Dopo l'IDT e non prima: il rilevamento imposta CR0.NE=1, cioe' "gli
     * errori di virgola mobile arrivano come eccezione #MF", e #MF ha
     * senso solo con un gate installato.
     *
     * E' un passo "b" per non rinumerare i tredici che seguono, che
     * compaiono nei log e nella documentazione.
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 7b] Inizializzazione FPU x87...");
    fpu_init();
klog(LOG_INFO, "[PASSO 7b] FPU %s", fpu_present() ? "OK" : "ASSENTE (x87 disabilitata)");

    /* =========================================================================
     * PASSO 8: Memory Manager (PLACEHOLDER — Fase 1c)
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 8] Inizializzazione Physical Memory Manager...");
    pmm_init(info);
klog(LOG_INFO, "[PASSO 8] PMM OK — %u pagine libere (%u MB)",
         pmm_get_free_pages(),
         (pmm_get_free_pages() * PAGE_SIZE) / (1024*1024));

    /* =========================================================================
     * PASSO 9: Paginazione (PLACEHOLDER — Fase 1d)
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 9] Inizializzazione paginazione...");
    isr_register_handler(14, page_fault_handler);   /* Registra handler #PF */
    paging_init();
klog(LOG_INFO, "[PASSO 9] Paginazione OK (CR0.PG=1)");

    klog(LOG_INFO, "[PASSO 9b] Inizializzazione heap kernel...");
    kmalloc_init();
kmalloc_stats();
    klog(LOG_INFO, "[PASSO 9b] Heap kernel OK");

/* =========================================================================
     * PASSO 10: Scheduler (PLACEHOLDER — Fase 2a)
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 10] Inizializzazione scheduler preemptive...");
    sched_init();

klog(LOG_INFO, "[PASSO 10] Scheduler OK (100Hz, round-robin, %d livelli priorita')",
         PRIO_MAX + 1);

    /* =========================================================================
     * PASSO 11: Syscall interface (PLACEHOLDER — Fase 2b)
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 11] Inizializzazione syscall interface (int 0x80)...");
    syscall_init();

klog(LOG_INFO, "[PASSO 11] Syscall OK");

    /* =========================================================================
     * PASSO 12: Abilita interrupt
     * SOLO DOPO che GDT, IDT e ISR sono installati e funzionanti.
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 12] Abilitazione interrupt...");
    interrupts_enable();

/* =========================================================================
     * PASSO 13: Driver, filesystem, shell (PLACEHOLDER — Fasi 3-5)
     * ========================================================================= */
    /* =========================================================================
     * PASSO 13: FAT12 kernel driver
     * ========================================================================= */
    /* Solo se si e' avviati DA floppy.
     *
     * Sondare l'FDC quando non c'e' un floppy non e' gratis: il driver
     * ritenta cinque volte con attese reali, e produce dodici righe di
     * ERROR/WARN a ogni avvio da disco. Chi legge quel log vede un
     * sistema che sembra rotto mentre sta funzionando — e il rumore
     * costante e' il modo migliore per non accorgersi dell'errore vero,
     * il giorno che arriva.
     *
     * Conseguenza da sapere: avviando da disco il floppy NON e'
     * raggiungibile. Montarlo richiedera' di separare "inizializza l'FDC"
     * da "monta la root", che oggi fat12_init fa insieme. */
    if (info->boot_drive < 0x80) {
        klog(LOG_INFO, "[PASSO 13] Inizializzazione FAT12 kernel driver...");
        if (fat12_init(info->boot_drive) != 0) {
            klog(LOG_WARN, "[PASSO 13] FAT12 init fallita — filesystem non disponibile");
        } else {
            klog(LOG_INFO, "[PASSO 13] FAT12 OK (drive=0x%02x)", info->boot_drive);
        }
    } else {
        klog(LOG_INFO, "[PASSO 13] Avvio da disco (0x%02x): FDC non sondato",
             info->boot_drive);
    }

    /* =========================================================================
     * PASSO 13a: Rilevamento dischi ATA/IDE
     *
     * ⚠️ ORDINE CAMBIATO nella 0.134, e non e' un riordino cosmetico.
     *
     * Prima questo passo veniva DOPO vfs_init(), perche' la root era per
     * forza il floppy e i dischi non servivano ad avviare. Da quando si
     * puo' avviare DA disco, vfs_init() deve poter montare come root una
     * partizione ATA — e per farlo i dispositivi a blocchi devono gia'
     * esistere.
     *
     * Un fallimento non e' fatale: un sistema senza dischi rigidi si
     * avvia esattamente come prima. Le attese del driver usano g_ticks,
     * quindi questo passo deve stare dopo l'abilitazione degli interrupt
     * (PASSO 12), come FAT12.
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 13a] Rilevamento dischi ATA/IDE...");
    {
        int n_dischi = ata_init();
        int n_cd;

        klog(LOG_INFO, "[PASSO 13a] ATA: %d disco/dischi rigidi", n_dischi);

        /* I lettori ottici li ha gia' RICONOSCIUTI ata_init: qui si
         * prendono in carico. Non si sonda il vassoio — un lettore con un
         * disco dentro puo' metterci secondi a dichiararsi pronto, e
         * pagarli a ogni avvio per un supporto che magari nessuno legge
         * non ha senso. Vedi atapi_init(). */
        n_cd = atapi_init();
        if (n_cd > 0)
            klog(LOG_INFO, "[PASSO 13a] ATAPI: %d lettore/i CD-DVD", n_cd);

        /* Il livello a blocchi va DOPO ata_init e atapi_init (legge le
         * tabelle delle partizioni e registra i lettori) e dopo
         * fat12_init (registra fd0). */
        blk_init();
    }

    /* Lo strato di montaggio va PRIMA di chiunque apra un file: da qui in
     * poi le syscall, la configurazione e il caricatore ELF passano da
     * vfs_*, e senza la tabella inizializzata ogni percorso risulterebbe
     * "non trovato" — compreso /bin/sh.
     *
     * Riceve il drive di avvio perche' e' lui a decidere che cosa sia la
     * root: il floppy (drive < 0x80) o la partizione attiva del disco. */
    vfs_init(info->boot_drive);

    /* =========================================================================
     * PASSO 13b: Lettura configurazione /boot/kernel.cfg
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 13b] Lettura /boot/kernel.cfg...");

KernelConfig *cfg = cfg_load();
    klog(LOG_INFO, "[PASSO 13b] Configurazione caricata");

    /* =========================================================================
     * PASSO 13d: montaggi automatici dalla sezione [mount]
     *
     * Qui e non prima: servono i dispositivi (13a) E il file di
     * configurazione (13b), che a sua volta si legge dal floppy.
     *
     * UN MONTAGGIO CHE FALLISCE NON FERMA L'AVVIO. E' una decisione, non
     * una svista: il disco elencato qui puo' essere stato tolto,
     * ripartizionato o formattato altrove, e nessuna di queste cose deve
     * rendere il sistema non avviabile. Si segnala e si prosegue —
     * l'alternativa, un kpanic, renderebbe un floppy di avvio funzionante
     * inutilizzabile per colpa di un disco che non serve ad avviare.
     * ========================================================================= */
    if (cfg->mount_count > 0) {
        uint32_t i, fatti = 0, saltati = 0;

        klog(LOG_INFO, "[PASSO 13d] Montaggi automatici: %u da applicare",
             cfg->mount_count);

        for (i = 0; i < cfg->mount_count; i++) {
            /* Il valore puo' finire con ",ro": "hd0p1,ro" monta in sola
             * lettura. Un suffisso e' preferibile a una chiave in piu'
             * perche' resta attaccato al montaggio che descrive. */
            char dev[CFG_NAME_LEN];
            int  j, ro = 0, r;

            for (j = 0; j < CFG_NAME_LEN - 1 && cfg->mounts[i].dev[j]; j++) {
                if (cfg->mounts[i].dev[j] == ',') break;
                dev[j] = cfg->mounts[i].dev[j];
            }
            dev[j] = '\0';
            if (cfg->mounts[i].dev[j] == ',') {
                const char *o = cfg->mounts[i].dev + j + 1;
                if (o[0] == 'r' && o[1] == 'o' && o[2] == '\0') ro = 1;
                else klog(LOG_WARN, "[PASSO 13d] opzione '%s' ignorata", o);
            }

            r = vfs_mount(dev, cfg->mounts[i].punto, ro);
            if (r == 0) { fatti++; continue; }

            /* UN LETTORE VUOTO NON E' UN PROBLEMA, ed e' l'unico caso in
             * cui questo passo tace invece di avvisare.
             *
             * La differenza conta per davvero da quando [mount] puo'
             * contenere un CD: un avvio senza disco nel lettore e' la
             * condizione NORMALE, e segnalarla con un [WARN] metterebbe a
             * ogni accensione una riga fra i "problemi durante
             * l'inizializzazione" dell'avvio silenzioso — cioe' l'esatto
             * rumore costante che quel registro esiste per evitare. Lo
             * stesso vale per un lettore che non c'e' affatto: una
             * configurazione che prevede il CD deve poter girare su una
             * macchina che non ce l'ha.
             *
             * Ogni altro fallimento resta un avviso: un disco rigido
             * elencato qui e non montabile e' una cosa da sapere. */
            if (r == ERR(ENOMEDIUM) ||
                (r == ERR(ENOENT) && dev[0] == 'c' && dev[1] == 'd')) {
                klog(LOG_INFO, "[PASSO 13d] '%s' su '%s': %s, montaggio saltato",
                     cfg->mounts[i].dev, cfg->mounts[i].punto,
                     (r == ERR(ENOMEDIUM)) ? "nessun disco nel lettore"
                                           : "lettore assente");
                saltati++;
                continue;
            }

            klog(LOG_WARN, "[PASSO 13d] '%s' su '%s' non montato (errore %d)",
                 cfg->mounts[i].dev, cfg->mounts[i].punto, r);
        }

        klog(LOG_INFO, "[PASSO 13d] %u montaggi su %u riusciti (%u saltati)",
             fatti, cfg->mount_count, saltati);
    }

    /* =========================================================================
     * PASSO 13c: applica verboseboot
     *
     * Con verboseboot=0 il sistema deve mostrare "solo l'output normale".
     * Servono due cose distinte, perché il rumore ha due sorgenti:
     *
     *  1. i messaggi FUTURI — dal PASSO 14 in poi e per tutta la vita del
     *     sistema, compresi quelli emessi quando la shell lancia un
     *     comando (`SYSCALL spawn`, `ELF: caricamento...`). Si zittiscono
     *     abbassando il livello di log a WARN: informativi e debug spariscono,
     *     avvisi ed errori NO. Un boot muto che nasconde un fallimento
     *     sarebbe peggio di un boot rumoroso, e la console seriale
     *     continua comunque a ricevere tutto (vga.c la specchia a monte
     *     del filtro di livello).
     *
     *  2. i messaggi GIÀ STAMPATI — PASSI 1-13, emessi prima che questa
     *     configurazione fosse leggibile (vedi il commento su
     *     print_boot_banner). Non si possono sopprimere a posteriori:
     *     si cancella lo schermo. È il motivo per cui questo passo esiste
     *     come punto separato invece di essere una riga dentro cfg_load.
     *
     *  3. i PROBLEMI già accaduti — che la pulizia dello schermo
     *     cancellerebbe insieme al resto. Sono la cosa che NON deve
     *     sparire: un driver non caricato, un timeout dell'FDC, il
     *     ripiego sulla tastiera in-kernel. klog() li registra sempre in
     *     un ring apposito (vedi kprintf.c), e qui vengono riproposti
     *     dopo la pulizia.
     *
     * Il nome del sistema resta visibile su una riga: verboseboot spegne
     * la diagnostica, non l'identità di ciò che si è avviato.
     *
     * Nota: il livello si abbassa a WARN, non a ERROR. Un avviso segnala
     * un evento inatteso da cui il sistema si è ripreso — esattamente ciò
     * che l'utente vuole vedere anche in un avvio silenzioso. Gli ERROR
     * sono comunque incondizionati: klog() li stampa qualunque sia il
     * livello (vedi il commento sopra klog in kprintf.c).
     * ========================================================================= */
    if (!cfg->verbose_boot) {
        uint32_t problems = klog_problem_count();

        klog_set_level(LOG_WARN);
        vga_clear();

        vga_setcolor(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        kprintf("%s\n\n", g_os_version_short);
        vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

        if (problems > 0) {
            vga_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            kprintf("  Avvio silenzioso: %u problema/i durante l'inizializzazione\n",
                    problems);
            vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            klog_replay_problems();
            kprintf("\n");
        }
    }

    /* =========================================================================
     * PASSO 14: TTY driver (tastiera + VGA)
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 14] Inizializzazione driver TTY...");
    {
        if (drv_init() != 0) {
            klog(LOG_WARN, "[PASSO 14] TTY init fallita");
        } else {
            klog(LOG_INFO, "[PASSO 14] TTY OK (output VGA)");
        }
    }
    /* La tastiera NON è ancora attiva: drv_init() non registra più
     * l'handler IRQ1, perché la scelta fra driver ring3 e percorso
     * in-kernel dipende dall'esito del PASSO 14b (vedi sotto). */

    /* =========================================================================
     * PASSO 15: Avvia shell come primo processo utente
     * Il percorso viene letto da kernel.cfg [boot] shell=...
     * ========================================================================= */
    /* =========================================================================
     * PASSO 14b: Driver — caricati come processi ring3 autonomi, non più
     * come moduli in kernel space. Ogni driver gira nel proprio spazio
     * di indirizzi, isolato dal kernel e dagli altri processi: un bug in
     * un driver può al più terminare quel processo (via #GP/#PF ->
     * proc_exit), mai corrompere il kernel o un altro driver. Il driver
     * stesso, appena avviato, chiama ipc_register(nome) per farsi
     * trovare dai client tramite sys_ipc_lookup — il kernel qui si
     * limita a caricarlo ed eseguirlo, senza conoscerne l'API interna.
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 14b] Caricamento driver come processi ring3...");
    int kbd_drv_loaded = 0;
    {
        uint32_t di;
        for (di = 0; di < CFG_MAX_MODULES; di++) {
            if (cfg->modules[di].name[0] == '\0') break;  /* fine elenco */

            Process *drv_proc = proc_create(cfg->modules[di].name, 0,
                                             PRIO_HIGH, 0);
            if (drv_proc == NULL) {
                klog(LOG_ERROR, "[PASSO 14b] Impossibile creare processo per driver '%s'",
                     cfg->modules[di].name);
                continue;
            }

            /* Adozione da parte di init.
             *
             * proc_create assegna come ppid il processo corrente, che qui
             * è il task idle: idle non muore mai e non chiama mai
             * waitpid(), quindi un driver che si schianta resterebbe
             * ZOMBIE per sempre. init_reaper_task() raccoglie solo gli
             * zombie con ppid == PID di init, e proc_exit() ri-genitorializza
             * a init solo gli orfani il cui padre è già morto — condizione
             * che con idle come padre non si verifica mai.
             *
             * Assegnando init come genitore fin da subito, la morte di un
             * driver libera davvero PCB, page directory, stack kernel e —
             * cosa che qui conta di più — il claim sull'IRQ e il nome IPC
             * registrato (proc_reap_zombie chiama irq_unbind_process e
             * ipc_cleanup_process). Senza questo, dopo un crash del driver
             * tastiera nessun altro processo potrebbe più registrarsi con
             * quel nome di servizio né rivendicare IRQ1. */
            if (g_init_task != NULL) {
                drv_proc->ppid = g_init_task->pid;
            }

            ElfLoadResult drv_res;
            if (elf_load(cfg->modules[di].path, drv_proc, &drv_res) == 0) {
                proc_set_entry(drv_proc, drv_res.entry_point, drv_res.user_stack_top);
                proc_set_ready(drv_proc);
                klog(LOG_INFO, "[PASSO 14b] Driver '%s' (%s) avviato: PID=%u entry=0x%08x",
                     cfg->modules[di].name, cfg->modules[di].path,
                     drv_proc->pid, drv_res.entry_point);
                if (str_equal(cfg->modules[di].name, KBD_SERVICE_NAME)) {
                    kbd_drv_loaded = 1;
                }
            } else {
                klog(LOG_WARN, "[PASSO 14b] Driver '%s': '%s' non caricato — saltato",
                     cfg->modules[di].name, cfg->modules[di].path);
                /* Il PCB era stato creato solo per dare a elf_load una page
                 * directory su cui mappare il driver: su fallimento va
                 * rilasciato COMPLETAMENTE, non solo marcato ZOMBIE.
                 *
                 * LEAK RISOLTO (luglio 2026): qui c'era il solo
                 * proc_kill(), che porta il processo a ZOMBIE. Nessuno
                 * poteva poi raccoglierlo: il reaper (init_reaper_task)
                 * raccoglie esclusivamente gli zombie con ppid == PID di
                 * init, mentre questi hanno come ppid il task che girava
                 * durante il boot (idle), che non chiama mai waitpid().
                 * Ogni driver mancante lasciava così, per sempre: uno slot
                 * del pool PCB, una page directory e le pagine dello stack
                 * kernel. Reap diretto sicuro: il processo è stato creato
                 * BLOCKED e non è mai stato eseguito, quindi non è
                 * g_current (precondizione di proc_reap_zombie). */
                proc_kill(drv_proc->pid);
                proc_reap_zombie(drv_proc);
            }
        }
    }
    klog(LOG_INFO, "[PASSO 14b] Caricamento driver completato");

    /* =========================================================================
     * PASSO 14c: sorgente dell'input del TTY
     *
     * Ora si sa se /dev/kbd.drv è stato caricato, quindi si può decidere
     * chi possiede la tastiera. Le due strade si escludono a vicenda:
     * irq_handler() (kernel/arch/x86/isr.c) consulta prima la tabella
     * degli handler kernel e solo se è vuota consegna la notifica IPC al
     * processo che ha rivendicato l'IRQ. Registrare l'handler interno
     * "per sicurezza" significherebbe quindi affamare il driver ring3.
     *
     * Attenzione: qui il driver è solo READY, non ha ancora eseguito una
     * singola istruzione — la sua ipc_register() avverrà più tardi. Il
     * TTY lo cerca pigramente al primo read() e riprova per qualche
     * decimo di secondo (vedi tty_read_ipc), quindi non serve
     * sincronizzarsi qui.
     * ========================================================================= */
    if (kbd_drv_loaded) {
        tty_set_input_source(TTY_INPUT_IPC);
        klog(LOG_INFO, "[PASSO 14c] Tastiera: driver ring3 '%s'", KBD_SERVICE_NAME);
    } else {
        tty_set_input_source(TTY_INPUT_INTERNAL);
        klog(LOG_WARN, "[PASSO 14c] Tastiera: driver ring3 assente, "
             "ripiego sull'handler IRQ1 in-kernel");
    }

    /* =========================================================================
     * PASSO 15: una shell per CONSOLE VIRTUALE
     *
     * Erano una sola, e la console pure. Con VGA_N_CONSOLE schermi
     * indipendenti ne serve una per ciascuno: e' il modello di Linux
     * (una getty per terminale) ed e' cio' che rende utile Alt+Fn —
     * passare alla console 2 deve dare un prompt pronto, non uno schermo
     * vuoto che non risponde.
     *
     * La console 0 e' anche quella di SISTEMA, dove il kernel stampa i
     * propri messaggi: la sua shell li vede scorrere accanto al prompt,
     * esattamente come prima che esistessero le altre.
     *
     * Se il caricamento di una fallisce non si abortisce il resto: una
     * console senza shell e' uno schermo inerte, quattro console senza
     * shell sono un sistema morto. Con almeno una viva si puo' ancora
     * lavorare e capire cosa e' andato storto.
     * ========================================================================= */
    klog(LOG_INFO, "[PASSO 15] Avvio di %u shell (una per console): %s",
         (unsigned)VGA_N_CONSOLE, cfg->shell_path);
    {
        uint32_t n;
        uint32_t avviate = 0;

        for (n = 0; n < VGA_N_CONSOLE; n++) {
            Process      *shell_proc;
            ElfLoadResult elf_res;
            char          nome[16];

            /* "sh0", "sh1", ... — il nome compare in `ps` e nei messaggi
             * dello scheduler, e distinguerle serve. */
            nome[0] = 's'; nome[1] = 'h';
            nome[2] = (char)('0' + n);
            nome[3] = '\0';

            /* proc_create con entry=0 crea il processo in stato BLOCKED:
             * non viene schedulato finché non chiamiamo proc_set_ready().
             * Questo ci permette di fare elf_load() con interrupt ABILITATI
             * (il driver FDC ne ha bisogno per i delay basati su g_ticks),
             * senza il rischio che lo scheduler salti a entry=0 prima che
             * l'ELF sia caricato. */
            shell_proc = proc_create(nome, 0, PRIO_NORMAL, 0);
            if (shell_proc == NULL) {
                klog(LOG_ERROR, "[PASSO 15] Impossibile creare la shell "
                     "della console %u!", n);
                continue;
            }

            /* Il legame fra processo e schermo. Da qui in poi ogni
             * write(1,...) di questa shell — e di tutto ciò che lancerà,
             * perché sys_spawn lo eredita — finisce su questa console. */
            shell_proc->console = n;

            if (elf_load(cfg->shell_path, shell_proc, &elf_res) == 0) {
                proc_set_entry(shell_proc, elf_res.entry_point, elf_res.user_stack_top);
                proc_set_ready(shell_proc);   /* ora è schedulabile */
                avviate++;
                klog(LOG_INFO, "[PASSO 15] Console %u: shell '%s' caricata "
                     "(PID %u, entry=0x%08x stack=0x%08x)",
                     n, cfg->shell_path, shell_proc->pid,
                     elf_res.entry_point, elf_res.user_stack_top);
            } else {
                /* NON dire "non trovata": elf_load fallisce per almeno una
                 * decina di ragioni diverse (file assente, header ELF non
                 * valido, errore di lettura a meta' di un segmento, memoria
                 * esaurita) e la riga "ELF: ..." stampata subito sopra dice
                 * quale. Il messaggio precedente affermava sempre la causa
                 * meno probabile, e sul Pentium II ha mandato la diagnosi
                 * fuori strada: il file c'era eccome, era la lettura del
                 * cilindro 10 a fallire. */
                klog(LOG_WARN, "[PASSO 15] Console %u: caricamento di '%s' "
                     "fallito (causa nella riga ELF: qui sopra)",
                     n, cfg->shell_path);
                /* Stesso motivo del PASSO 14b: rilascia tutte le risorse
                 * del PCB scratch, non lasciarlo ZOMBIE non raccoglibile. */
                proc_kill(shell_proc->pid);
                proc_reap_zombie(shell_proc);
            }
        }

        if (avviate == 0) {
            klog(LOG_ERROR, "[PASSO 15] Nessuna shell avviata — "
                 "il sistema resta senza console");
        }
    }

    /* Il pipeline proc_create+elf_load+proc_set_entry+proc_set_ready è
     * stato verificato stabile con un processo di test (/bin/hello):
     * nessun freeze, tutte le fasi completano correttamente. */

    /* =========================================================================
     * Banner finale
     * ========================================================================= */
    /* Banner finale: solo se verboseboot lo consente. Con verboseboot=0
     * l'identità del sistema è già stata stampata su una riga al PASSO
     * 13c, e ripeterla qui in un riquadro sarebbe esattamente il rumore
     * che si è chiesto di togliere. */
    if (cfg->verbose_boot) {
        vga_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        kprintf("\n");
        kprintf("  ============================================\n");
        kprintf("%s\n", g_os_version);
        kprintf("   Sistema avviato. Scheduler 100Hz attivo.\n");
        kprintf("  ============================================\n");
        vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        kprintf("\n");
    }

    /* Boot finito: nessun trasferimento in corso, il motore del floppy
     * puo' fermarsi. Senza questa riga resterebbe acceso per sempre (il
     * driver non ha un timer di inattivita' — vedi fat12_motor_off), e su
     * una macchina vera il dischetto continuerebbe a girare sotto le
     * testine per tutta la sessione. Il primo accesso al disco lo
     * riaccende pagando una sola volta i 300 ms di stabilizzazione. */
    fat12_motor_off();

    klog(LOG_INFO, "Avvio scheduler — sblocco timer e cessione controllo.");
    if (cfg->verbose_boot) sched_dump();

    /* sched_start() sblocca IRQ0 e non ritorna mai: da qui in poi il
     * kernel è guidato interamente dagli interrupt (timer per i context
     * switch, INT 0x30 per le syscall dei task ring3). */

sched_start();
}

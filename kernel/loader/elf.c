/* =============================================================================
 * kernel/loader/elf.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "elf.h"
#include "fat12.h"
#include "vfs.h"
#include "pmm.h"
#include "paging.h"
#include "sched.h"
#include "kmalloc.h"
/* Per ERR()/ENOENT: il motivo del fallimento di vfs_open viene reso al
 * chiamante invece di essere appiattito su -1. Vedi elf_carica. */
#include "syscall.h"

/* =============================================================================
 * Strutture ELF32 (formato file)
 * ============================================================================= */

/* ELF Magic */
/* ! IL FORMATO ELF STA IN elf.h, NON QUI, dal 17 agosto 2026: lo leggono in
 * due — questo caricatore e quello delle librerie condivise (loader/lib.c).
 * Due copie della stessa struttura che attraversa un confine sono la stessa
 * trappola in cui e' caduta SpawnExtra: vedi lib/include/spawn_abi.h. */

/* =============================================================================
 * elf_verify_header — Controlla che l'ELF header sia valido per ExOS
 * ============================================================================= */
static int elf_verify_header(const Elf32Header *hdr)
{
    /* Magic number */
    if (hdr->e_ident[EI_MAG0] != ELF_MAGIC0 ||
        hdr->e_ident[EI_MAG1] != ELF_MAGIC1 ||
        hdr->e_ident[EI_MAG2] != ELF_MAGIC2 ||
        hdr->e_ident[EI_MAG3] != ELF_MAGIC3) {
        klog(LOG_ERROR, "ELF: magic non valido");
        return -1;
    }

    /* Classe 32-bit */
    if (hdr->e_ident[EI_CLASS] != 1) {
        klog(LOG_ERROR, "ELF: non e' ELF32 (class=%u)", hdr->e_ident[EI_CLASS]);
        return -1;
    }

    /* Little-endian */
    if (hdr->e_ident[EI_DATA] != 1) {
        klog(LOG_ERROR, "ELF: non e' little-endian");
        return -1;
    }

    /* Tipo: eseguibile */
    if (hdr->e_type != ET_EXEC) {
        klog(LOG_ERROR, "ELF: tipo non supportato (type=%u, atteso ET_EXEC=%u)",
             hdr->e_type, ET_EXEC);
        return -1;
    }

    /* Architettura i386 */
    if (hdr->e_machine != EM_386) {
        klog(LOG_ERROR, "ELF: architettura non supportata (machine=%u)", hdr->e_machine);
        return -1;
    }

    /* Versione */
    if (hdr->e_version != 1) {
        klog(LOG_ERROR, "ELF: versione non valida (%u)", hdr->e_version);
        return -1;
    }

    /* Entry point nel range utente */
    if (hdr->e_entry < USER_SPACE_BASE || hdr->e_entry >= USER_SPACE_END) {
        klog(LOG_ERROR, "ELF: entry point fuori spazio utente: 0x%08x", hdr->e_entry);
        return -1;
    }

    /* Program headers presenti */
    if (hdr->e_phnum == 0 || hdr->e_phoff == 0) {
        klog(LOG_ERROR, "ELF: nessun program header");
        return -1;
    }

    return 0;
}

/* =============================================================================
 * elf_load — Carica un ELF da FAT12 in un processo
 *
 * path:    percorso del file ELF (es. "/bin/sh")
 * proc:    processo destinazione (PD già creata)
 * result:  struttura con entry point e stack top da passare a sched_enter_usermode
 *
 * Ritorna: 0 = successo, -1 = errore
 * ============================================================================= */
/* =============================================================================
 * CARICAMENTO SU RICHIESTA (dal 0.149)
 *
 * elf_carica() ha due modi, e la differenza sta tutta nel Passo 5.
 *
 *   RESIDENTE  — il comportamento storico: ogni pagina di ogni PT_LOAD
 *                viene allocata e riempita subito, il file si chiude e
 *                dell'eseguibile non resta traccia.
 *
 *   SU RICHIESTA — si annota soltanto DOVE ogni segmento vive nel file
 *                (proc->vma) e si tiene l'eseguibile aperto. Le pagine
 *                arrivano una per volta quando il processo le tocca, da
 *                pf_carica_da_file(). Il costo d'avvio smette di dipendere
 *                dalla dimensione del binario: un programma da 40 MB di
 *                cui si esegue una funzione occupa le pagine di quella
 *                funzione. Senza questo, ospitare un compilatore vuol dire
 *                impegnare decine di MB prima della prima istruzione.
 *
 * ! I DRIVER SI CARICANO RESIDENTI, e non e' prudenza generica: un driver
 * che serve il filesystem, paginato DA quel filesystem, dovrebbe servire la
 * propria lettura mentre e' fermo in attesa di quella lettura. Si blocca, e
 * con lui il sistema. Sono due file da ~15 KB: non c'e' niente da
 * risparmiare e c'e' un blocco da evitare.
 * ============================================================================= */
/* =============================================================================
 * percorso_di_driver — l'unico posto dove si decide chi tocca l'hardware
 *
 * Un eseguibile e' un driver se si chiama <qualcosa>.drv. Da qui esce il flag
 * `is_driver` del PCB, su cui si appoggiano ioport_bind, dma_alloc e mmio_map
 * (kernel/syscall/syscall_impl.c).
 *
 * ! IL PUNTO NON E' LA FORZA DELLA REGOLA: E' CHI RISPONDE ALLA DOMANDA.
 * Prima il kernel chiedeva «hai delle porte I/O?», e le porte se le prendeva
 * il processo stesso chiamando ioport_bind, che non verificava niente — una
 * domanda a cui bastava rispondere di si', posta a chi aveva interesse a
 * mentire. Il nome dell'eseguibile invece e' un fatto fissato PRIMA che il
 * programma parta, e il caricatore lo legge quando il processo non esiste
 * ancora.
 *
 * ! NON E' UNA DIFESA, E VA DETTO CHIARO. EX-OS non ha proprietari dei file:
 * qualunque programma puo' copiarsi in «qualcosa.drv» e ripartire da li'. Una
 * barriera vera vuole un concetto di proprietario, e arrivera' con quello.
 * Questa e' una DICHIARAZIONE — «i driver sono questi» — e serve a tre cose
 * che il criterio di prima non faceva:
 *
 *   - non e' circolare: non si ottiene chiamando una syscall;
 *   - non esclude chi driver lo e' davvero ma porte I/O non ne ha (il
 *     framebuffer: vedi DIREZIONE.md, gradino 0);
 *   - lascia una traccia sul disco invece di due righe dentro un programma.
 *
 * ! SI GUARDA IL NOME, NON LA DIRECTORY, e la ragione e' che le directory
 * sono gia' tre e diventeranno di piu': /dev sul sistema installato e sul
 * floppy, /cdrom/dev e /cdrom/drivers sul CD. Una regola su «deve stare in
 * /dev» avrebbe respinto ogni driver lanciato dal lettore — cioe' tutti
 * quelli di rete — e l'avrebbe fatto con un EPERM che parla di permessi
 * mentre il problema e' il percorso.
 *
 * `.drv` e' la convenzione del progetto da sempre, ed e' il Makefile a
 * imporla: e' li' che i driver prendono quel nome, uno per uno.
 * ========================================================================== */
static int percorso_di_driver(const char *path)
{
    const char *nome;
    const char *p;
    uint32_t    len = 0;

    if (path == NULL) return 0;

    /* L'ultima componente: il nome del file. */
    nome = path;
    for (p = path; *p != '\0'; p++)
        if (*p == '/') nome = p + 1;

    while (nome[len] != '\0') len++;
    if (len <= 4) return 0;                 /* ".drv" da solo non e' un nome */

    return nome[len - 4] == '.' && nome[len - 3] == 'd' &&
           nome[len - 2] == 'r' && nome[len - 1] == 'v';
}

static int elf_carica(const char *path, Process *proc, ElfLoadResult *result,
                      int residente, uint32_t stack_extra)
{
    int           handle;
    Elf32Header   hdr;
    Elf32Phdr    *phdrs = NULL;
    uint8_t      *seg_buf = NULL;
    uint32_t      i;
    int           ret = -1;

    klog(LOG_INFO, "ELF: caricamento '%s'...", path);

    /* exec sostituisce l'immagine: l'eseguibile di prima non serve piu' e
     * il suo handle non deve restare aperto per sempre. */
    if (proc != NULL && proc->exe_handle >= 0) {
        vfs_close(proc->exe_handle);
        proc->exe_handle = -1;
    }
    if (proc != NULL) proc->n_vma = 0;

    /* ==========================================================================
     * Passo 1: Apri il file ELF dal FAT12
     * ========================================================================== */

/* =========================================================================
     * ! IL PERMESSO DI ESEGUIRE SI CHIEDE PRIMA DI APRIRE, e l'ordine non e'
     * indifferente: aprire e poi rifiutare vorrebbe dire aver gia' toccato il
     * filesystem — e su EX-OS aprire un file e' un giro di IPC verso un driver
     * in ring 3, cioe' un punto di riscadenzamento. Chiedere prima costa una
     * stat e non lascia niente a meta'.
     *
     * ! ED E' LA RIGA CHE RENDE IL VARCO `*.drv` UNA BARRIERA. Finora era una
     * definizione — bastava copiarsi in `x.drv` — perche' non c'era niente che
     * impedisse a un utente di scrivere in /dev. Adesso /dev e' di root, e un
     * .drv che l'utente si e' fatto altrove non lo esegue se non lo possiede.
     *
     * ! ROOT E CHI NON HA UN PROCESSO PASSANO. Il primo processo lo crea il
     * kernel, quando ancora non c'e' nessuno a cui attribuire la richiesta:
     * vfs_uid_corrente() rende 0, e la shell d'avvio parte. */
    {
        int perm = vfs_eseguibile(path);

        if (perm != 0) {
            klog(perm == ERR(EACCES) ? LOG_WARN : LOG_INFO,
                 "ELF: '%s' non eseguibile da questo utente (err=%d)", path, perm);
            return perm;
        }
    }

handle = vfs_open(path, 0x0000);  /* O_RDONLY */

if (handle < 0) {
        /* ! UN FILE CHE NON C'E' NON E' UN ERRORE DEL KERNEL: e' la
         * risposta a una domanda, e chi l'ha posta la riceve come ENOENT e
         * la riferisce lui. Cercare un comando nel PATH vuol dire fare
         * questa domanda una volta per voce, e tutte tranne l'ultima hanno
         * risposta «no» anche quando va tutto bene: a LOG_ERROR — che
         * kernel.cfg stampa SEMPRE, qualunque sia loglevel — un `pippo`
         * battuto per sbaglio riempiva lo schermo di sei righe rosse prima
         * dell'unico messaggio che l'utente doveva leggere.
         *
         * Ogni ALTRO motivo resta un errore vero: li' il file c'e' e non si
         * riesce a prenderlo, e quello nessuno lo saprebbe altrimenti. */
        klog(handle == ERR(ENOENT) ? LOG_INFO : LOG_ERROR,
             "ELF: apertura fallita: '%s' (err=%d)", path, handle);
        /* Si rende il MOTIVO, non un generico -1: e' cosi' che sys_spawn
         * distingue «non c'e'» da «c'e' e non si carica» e lo dice alla
         * shell, che altrimenti proseguirebbe la ricerca nel PATH e
         * concluderebbe «comando non trovato» su un binario esistente. */
        return handle;
    }

    /* ==========================================================================
     * Passo 2: Leggi e verifica ELF header
     * ========================================================================== */

if (vfs_read(handle, &hdr, sizeof(Elf32Header), 0) != (int)sizeof(Elf32Header)) {
        klog(LOG_ERROR, "ELF: impossibile leggere header");
        goto cleanup;
    }

    if (elf_verify_header(&hdr) != 0) goto cleanup;

klog(LOG_INFO, "ELF: header valido, entry=0x%08x, phnum=%u",
         hdr.e_entry, hdr.e_phnum);

    /* ==========================================================================
     * Passo 3: Leggi tutti i Program Headers
     * ========================================================================== */
    uint32_t phdrs_size = hdr.e_phnum * sizeof(Elf32Phdr);
    phdrs = (Elf32Phdr *)kmalloc(phdrs_size);
    if (!phdrs) {
        klog(LOG_ERROR, "ELF: OOM allocando program headers");
        goto cleanup;
    }

    if (vfs_read(handle, phdrs, phdrs_size, hdr.e_phoff) != (int)phdrs_size) {
        klog(LOG_ERROR, "ELF: impossibile leggere program headers");
        goto cleanup;
    }

    /* ==========================================================================
     * Passo 4: Alloca buffer temporaneo per i segmenti
     * Useremo un buffer di 1 pagina alla volta per non sprecare RAM.
     * ========================================================================== */

seg_buf = (uint8_t *)kmalloc(PAGE_SIZE);
    if (!seg_buf) {
        klog(LOG_ERROR, "ELF: OOM allocando buffer segmento");
        goto cleanup;
    }

    /* ==========================================================================
     * Passo 5: Processa ogni PT_LOAD
     * ========================================================================== */

    /* Piu' segmenti di quanti il PCB ne sappia annotare: si carica tutto in
     * RAM invece di mappare i primi e dimenticare gli altri. Un binario
     * mappato a meta' non da' errore — da' un salto nel vuoto quando il
     * programma arriva nel pezzo che manca. */
    if (!residente) {
        uint32_t n_load = 0;
        for (i = 0; i < hdr.e_phnum; i++)
            if (phdrs[i].p_type == PT_LOAD && phdrs[i].p_memsz != 0) n_load++;
        if (n_load > PROC_MAX_VMA) {
            klog(LOG_WARN, "ELF: '%s' ha %u segmenti (max %u su richiesta): "
                 "caricamento residente", path, n_load, (unsigned)PROC_MAX_VMA);
            residente = 1;
        }
    }

for (i = 0; i < hdr.e_phnum; i++) {
        Elf32Phdr *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0)      continue;

        klog(LOG_INFO, "ELF: PT_LOAD[%u] vaddr=0x%08x memsz=0x%x filesz=0x%x flags=0x%x",
             i, ph->p_vaddr, ph->p_memsz, ph->p_filesz, ph->p_flags);

        /* Verifica range virtuale */
        if (ph->p_vaddr < USER_SPACE_BASE || ph->p_vaddr >= USER_SPACE_END) {
            klog(LOG_ERROR, "ELF: segmento fuori spazio utente: 0x%08x", ph->p_vaddr);
            goto cleanup;
        }

        /* Calcola pagine necessarie */
        uint32_t vstart  = ALIGN_DOWN(ph->p_vaddr, PAGE_SIZE);
        uint32_t vend    = ALIGN_UP(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
        uint32_t pages   = (vend - vstart) / PAGE_SIZE;
        uint32_t pg_flags = PG_PRESENT | PG_USER;
        if (ph->p_flags & PF_W) pg_flags |= PG_WRITABLE;

        /* --- SU RICHIESTA: si annota e basta ------------------------------
         *
         * file_off e' l'offset che corrisponde a vstart, non a p_vaddr: il
         * segmento comincia quasi sempre a meta' pagina, e la parte prima
         * di p_vaddr appartiene al file quanto il resto (e' il contenuto
         * che sta nella stessa pagina). Sottrarre lo scarto e' cio' che
         * rende l'offset calcolabile con una sola somma a ogni fault. */
        if (!residente) {
            ProcVma *v = &proc->vma[proc->n_vma++];

            v->vstart    = vstart;
            v->vend      = vend;
            v->file_off  = ph->p_offset - (ph->p_vaddr - vstart);
            v->file_fine = ph->p_vaddr + ph->p_filesz;
            v->pg_flags  = pg_flags;

            klog(LOG_INFO, "ELF: segmento su richiesta 0x%08x-0x%08x "
                 "(%u pagine, file@0x%x, dati fino a 0x%08x)",
                 vstart, vend, pages, v->file_off, v->file_fine);
            continue;
        }

/* Alloca e mappa le pagine */
        uint32_t pg;
        for (pg = 0; pg < pages; pg++) {
            uint32_t phys = pmm_alloc_page();
            if (phys == 0) {
                klog(LOG_ERROR, "ELF: OOM allocando pagina per segmento %u", i);
                goto cleanup;
            }

            /* Azzera la pagina. Attraverso la finestra di rimappatura:
             * qui gira ancora il processo CHIAMANTE (la shell che ha
             * fatto spawn), la cui page directory mappa per identita'
             * solo la fascia kernel — e questa pagina puo' stare
             * ovunque in RAM. Vedi paging_finestra_apri(). */
            paging_azzera_fisica(phys);

            uint32_t vpage = vstart + pg * PAGE_SIZE;
            if (paging_map_page(proc->page_directory, vpage, phys, pg_flags) != 0) {
                pmm_free_page(phys);
                klog(LOG_ERROR, "ELF: errore mapping pagina 0x%08x", vpage);
                goto cleanup;
            }
        }

/* Copia dati dal file nella memoria virtuale del processo
         * Dobbiamo scrivere attraverso il mapping: traduciamo ogni
         * indirizzo virtuale → fisico e scriviamo direttamente. */
        if (ph->p_filesz > 0) {
            uint32_t file_offset = ph->p_offset;
            uint32_t vaddr       = ph->p_vaddr;
            uint32_t remaining   = ph->p_filesz;

            while (remaining > 0) {
                uint32_t chunk = remaining;
                if (chunk > PAGE_SIZE) chunk = PAGE_SIZE;

                /* Leggi chunk dal file ELF */
                int n = vfs_read(handle, seg_buf, chunk, file_offset);

if (n <= 0) {
                    klog(LOG_ERROR, "ELF: errore lettura segmento (offset=%u)", file_offset);
                    goto cleanup;
                }

                /* Traduci indirizzo virtuale → fisico e copia */
                uint32_t written = 0;
                while (written < (uint32_t)n) {
                    uint32_t phys_dst = paging_get_physical(proc->page_directory,
                                                             vaddr + written);
                    if (phys_dst == 0) {
                        klog(LOG_ERROR, "ELF: indirizzo virtuale non mappato 0x%08x",
                             vaddr + written);
                        goto cleanup;
                    }

                    /* Copia byte per byte (potremmo ottimizzare con memcpy) */
                    uint32_t page_off = (vaddr + written) & 0xFFF;
                    uint32_t avail    = PAGE_SIZE - page_off;
                    uint32_t take     = (uint32_t)n - written;
                    if (take > avail) take = avail;

                    /* Finestra aperta e chiusa intorno alla SINGOLA
                     * copia, mai intorno al ciclo: fra un giro e l'altro
                     * c'e' la vfs_read qui sopra, che si blocca in IPC
                     * verso un driver in ring3 — e una finestra tenuta
                     * aperta attraverso un blocco e' una finestra che
                     * qualcun altro ripunta sotto i piedi. */
                    uint8_t *dst_ptr = (uint8_t *)paging_finestra_apri(
                                           phys_dst - page_off);
                    uint32_t k;
                    for (k = 0; k < take; k++) {
                        dst_ptr[page_off + k] = seg_buf[written + k];
                    }
                    paging_finestra_chiudi();
                    written += take;
                }

                file_offset += (uint32_t)n;
                vaddr       += (uint32_t)n;
                remaining   -= (uint32_t)n;
            }
        }

/* BSS: la zona tra filesz e memsz è già azzerata (pagine azzerate sopra) */
        klog(LOG_DEBUG, "ELF: segmento %u caricato OK", i);
    }

/* ==========================================================================
     * Passo 6: Alloca stack utente
     *
     * Lo stack cresce verso il basso. Mettiamo il top a USER_SPACE_END - 4KB
     * e allochiamo USER_STACK_SIZE byte.
     * ========================================================================== */
    {
        uint32_t stack_top   = USER_SPACE_END - PAGE_SIZE;
        uint32_t stack_limit = stack_top - USER_STACK_MAX;   /* riserva */
        uint32_t stack_init  = USER_STACK_INIT;              /* impegno */
        uint32_t stack_base;
        uint32_t stack_pages;
        uint32_t pg;

        /* ! CHI PORTA UNA RIGA DI COMANDO LUNGA SE LA PAGA IN ANTICIPO.
         * Le stringhe di argv le scrive sys_spawn dall'esterno, quando il
         * figlio non e' ancora in esecuzione: una pagina non mappata non
         * puo' generare il fault che la mapperebbe, quindi lo spazio
         * dev'esserci PRIMA. Vedi elf_load_argv in kernel/include/elf.h.
         *
         * Si somma a USER_STACK_INIT invece di sostituirlo: quegli otto
         * kilobyte sono il margine con cui il programma PARTE, e mangiarli
         * per farci stare argv darebbe un processo che nasce con lo stack
         * gia' pieno — un fault immediato invece di un errore qui. */
        if (stack_extra > 0) {
            uint32_t serve = USER_STACK_INIT + stack_extra;

            serve = (serve + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1u);
            if (serve > USER_STACK_MAX) {
                /* Non si tronca in silenzio: una riga di comando piu' larga
                 * della riserva intera e' un errore di chi la costruisce, e
                 * accorciarla qui la farebbe scoprire al programma lanciato,
                 * che parlerebbe dei PROPRI argomenti. */
                klog(LOG_ERROR, "ELF: argv chiede %u KB di stack, la riserva "
                     "e' %u KB", serve / 1024, USER_STACK_MAX / 1024);
                goto cleanup;
            }
            stack_init = serve;
        }

        stack_base  = stack_top - stack_init;
        stack_pages = stack_init / PAGE_SIZE;

        /* SI IMPEGNANO SOLO LE PRIME PAGINE (kernel 0.124).
         *
         * Prima qui si allocavano e azzeravano tutti i 64 KB, byte per
         * byte, per OGNI processo — anche per un programma che ne usa
         * duecento. Erano ~65000 iterazioni di azzeramento a ogni
         * caricamento, spese quasi interamente su pagine mai toccate.
         *
         * Ora si impegnano USER_STACK_INIT byte partendo dal TOP (lo
         * stack cresce verso il basso, quindi le pagine subito utili sono
         * quelle in cima) e il resto della riserva viene mappato su
         * richiesta da page_fault_handler. Il caricamento e' piu' veloce e
         * il processo occupa meno RAM; il tetto resta USER_STACK_MAX. */
        for (pg = 0; pg < stack_pages; pg++) {
            uint32_t phys = pmm_alloc_page();
            if (phys == 0) {
                klog(LOG_ERROR, "ELF: OOM allocando stack utente");
                goto cleanup;
            }
            /* Azzera (via finestra: vedi sopra) */
            paging_azzera_fisica(phys);

            uint32_t vpage = stack_base + pg * PAGE_SIZE;
            if (paging_map_page(proc->page_directory, vpage, phys,
                                 PG_PRESENT | PG_WRITABLE | PG_USER) != 0) {
                pmm_free_page(phys);
                goto cleanup;
            }
        }

        proc->user_stack_base  = stack_base;
        proc->user_stack_top   = stack_top;
        proc->user_stack_limit = stack_limit;
        result->user_stack_top = stack_top - 16;   /* Allineamento 16 byte */

        klog(LOG_INFO, "ELF: stack utente 0x%08x - 0x%08x impegnato "
             "(%u KB, riserva fino a 0x%08x, %u KB)",
             stack_base, stack_top, stack_init / 1024, stack_limit,
             USER_STACK_MAX / 1024);
    }

    /* ==========================================================================
     * Passo 6b: il blocco TLS, se il programma ne ha uno
     *
     * PT_TLS non e' un segmento da caricare: e' l'IMMAGINE DI PARTENZA
     * delle variabili __thread. Il caricatore ne fa una copia per processo
     * e ci punta il thread pointer; il segmento originale non viene mai
     * mappato ne' usato direttamente.
     *
     * DISPOSIZIONE (variante II di i386, modello local-exec):
     *
     *      +----------------------+-------------------+
     *      |  blocco TLS (memsz)  |  TCB (8 byte)     |
     *      +----------------------+-------------------+
     *      ^ tls_base             ^ tp = tls_tp
     *
     * Il TCB comincia con un puntatore a SE STESSO: e' la convenzione ABI,
     * ed e' cio' che rende `mov %gs:0x0, %ebx` — la prima istruzione che
     * emette GCC per accedere a una variabile thread-local — una lettura
     * del thread pointer invece che di una variabile qualunque. Le
     * variabili stanno a offset NEGATIVI da tp, gia' risolti da `ld`.
     *
     * DOVE: sotto la riserva dello stack, con una pagina di guardia in
     * mezzo. ! La guardia non e' un vezzo: senza, una ricorsione infinita
     * scenderebbe dallo stack dentro il blocco TLS — che e' mappato — e
     * invece di terminare il processo con "stack esaurito" ne
     * corromperebbe le variabili in silenzio.
     * ========================================================================== */
    {
        Elf32Phdr *tls = NULL;

        for (i = 0; i < hdr.e_phnum; i++) {
            if (phdrs[i].p_type == PT_TLS && phdrs[i].p_memsz != 0) {
                tls = &phdrs[i];
                break;
            }
        }

        /* ! IL BLOCCO SI FA ANCHE A CHI NON HA VARIABILI __thread, dal 4
         * settembre 2026, e ridotto al solo TCB: otto byte in una pagina.
         *
         * Serve alla libc, che tiene errno per filo e per trovarlo legge il
         * thread pointer da `%gs:0`. Con un processo senza blocco, la base di
         * quel descrittore vale ZERO e leggere `%gs:0` non da' un valore
         * sbagliato: da' un page fault all'indirizzo 0. Una pagina per
         * processo e' il prezzo di poter scrivere quella lettura senza un
         * «se». */
        {
            uint32_t align   = tls ? ((tls->p_align < 4) ? 4 : tls->p_align) : 4;
            uint32_t dim_tls = tls ? ALIGN_UP(tls->p_memsz, align) : 0;
            uint32_t totale  = ALIGN_UP(dim_tls + TLS_TCB_SIZE, PAGE_SIZE);
            uint32_t guardia = proc->user_stack_limit - PAGE_SIZE;
            uint32_t base    = ALIGN_DOWN(guardia - totale, PAGE_SIZE);
            uint32_t tp      = base + dim_tls;
            uint32_t pg;

            if (dim_tls > TLS_MAX) {
                klog(LOG_ERROR, "ELF: blocco TLS di %u byte, massimo %u",
                     dim_tls, (unsigned)TLS_MAX);
                goto cleanup;
            }

            for (pg = 0; pg < totale / PAGE_SIZE; pg++) {
                uint32_t phys = pmm_alloc_page();
                if (phys == 0) {
                    klog(LOG_ERROR, "ELF: OOM allocando il blocco TLS");
                    goto cleanup;
                }
                /* Azzerata: e' la parte .tbss, e ci si appoggia anche per i
                 * byte fra p_filesz e p_memsz. */
                paging_azzera_fisica(phys);
                if (paging_map_page(proc->page_directory, base + pg * PAGE_SIZE,
                                    phys, PG_PRESENT | PG_WRITABLE | PG_USER) != 0) {
                    pmm_free_page(phys);
                    goto cleanup;
                }
            }

            /* L'immagine iniziale (.tdata) si legge dal file: qui non si
             * puo' fare altrimenti, perche' con il caricamento su richiesta
             * il segmento non e' in RAM da nessuna parte. */
            if (tls != NULL && tls->p_filesz > 0) {
                uint32_t rimasti = tls->p_filesz;
                uint32_t off     = tls->p_offset;
                uint32_t vaddr   = base;

                while (rimasti > 0) {
                    uint32_t quanti = (rimasti > PAGE_SIZE) ? PAGE_SIZE : rimasti;
                    int      n = vfs_read(handle, seg_buf, quanti, off);
                    uint32_t k, scritti = 0;

                    if (n <= 0) {
                        klog(LOG_ERROR, "ELF: lettura dell'immagine TLS fallita");
                        goto cleanup;
                    }

                    while (scritti < (uint32_t)n) {
                        uint32_t page_off = (vaddr + scritti) % PAGE_SIZE;
                        uint32_t take     = PAGE_SIZE - page_off;
                        uint32_t phys_dst;
                        uint8_t *dst;

                        if (take > (uint32_t)n - scritti) take = (uint32_t)n - scritti;

                        phys_dst = paging_get_physical(proc->page_directory,
                                                       vaddr + scritti);
                        if (phys_dst == 0) goto cleanup;

                        dst = (uint8_t *)paging_finestra_apri(phys_dst - page_off);
                        for (k = 0; k < take; k++) dst[page_off + k] = seg_buf[scritti + k];
                        paging_finestra_chiudi();

                        scritti += take;
                    }

                    off     += (uint32_t)n;
                    vaddr   += (uint32_t)n;
                    rimasti -= (uint32_t)n;
                }
            }

            /* Il puntatore a se stesso in testa al TCB. */
            {
                uint32_t phys_tcb = paging_get_physical(proc->page_directory, tp);
                uint32_t off_tcb  = tp % PAGE_SIZE;
                uint32_t *tcb;

                if (phys_tcb == 0) goto cleanup;
                tcb = (uint32_t *)paging_finestra_apri(phys_tcb - off_tcb);
                tcb[off_tcb / 4] = tp;
                paging_finestra_chiudi();
            }

            proc->tls_base = base;
            proc->tls_tp   = tp;

            /* Le coordinate per rifarlo a ogni filo: vedi proc_thread_crea. */
            proc->tls_off    = tls ? tls->p_offset : 0;
            proc->tls_filesz = tls ? tls->p_filesz  : 0;
            proc->tls_dim    = dim_tls;

            klog(LOG_INFO, "ELF: TLS 0x%08x-0x%08x, tp=0x%08x "
                 "(memsz=%u filesz=%u align=%u)",
                 base, base + totale, tp,
                 tls->p_memsz, tls->p_filesz, tls->p_align);
        }
    }

    /* ==========================================================================
     * Passo 7: Aggiorna heap del processo
     * ========================================================================== */
    {
        /* L'heap inizia subito dopo l'ultimo segmento caricato */
        uint32_t heap_start = 0;
        for (i = 0; i < hdr.e_phnum; i++) {
            if (phdrs[i].p_type != PT_LOAD) continue;
            uint32_t end = ALIGN_UP(phdrs[i].p_vaddr + phdrs[i].p_memsz, PAGE_SIZE);
            if (end > heap_start) heap_start = end;
        }
        proc->heap_start = heap_start;
        proc->heap_end   = heap_start;

        /* IL TETTO (kernel 0.156). Sopra lo heap non c'e' il vuoto: c'e'
         * il blocco TLS, e sopra quello la riserva dello stack. Senza un
         * confine, sbrk cresceva finche' il PMM aveva pagine — e
         * paging_map_page() sovrascrive in silenzio, quindi il danno
         * sarebbe stato il TLS rimappato su pagine azzerate invece di un
         * errore. Vedi il commento in kernel/include/sched.h.
         *
         * Una pagina di guardia sotto il primo oggetto che c'e' davvero:
         * il blocco TLS se il programma ne ha uno, altrimenti la riserva
         * dello stack. La guardia non e' mappata da nessuno, quindi un
         * accesso li' dentro e' un fault e non una corruzione. */
        {
            uint32_t soffitto = proc->tls_base ? proc->tls_base
                                               : proc->user_stack_limit;

            /* =============================================================
             * ! SOTTO IL SOFFITTO SI RISERVA LA BANDA DEGLI STACK DEI FILI,
             * e la si riserva A TUTTI — anche a un programma che un filo non
             * lo fara' mai.
             *
             * Sono INDIRIZZI, non pagine: mezzo megabyte di spazio di
             * indirizzamento in meno per uno heap che ne ha tre giga. La
             * scelta si capisce guardando l'alternativa: riservarla quando
             * nasce il primo filo vorrebbe dire abbassare heap_max sotto
             * memoria che lo heap potrebbe GIA' avere preso — e allora o si
             * rifiuta il filo, o si mette il suo stack sopra lo heap di
             * qualcun altro. Il primo caso e' un limite che salta fuori a
             * caso, il secondo e' memoria corrotta in silenzio.
             *
             *   heap ... heap_max | guardia | banda dei fili | TLS | stack
             * ============================================================= */
            proc->fili_banda = (soffitto > FILI_BANDA) ? soffitto - PAGE_SIZE : 0;
            if (proc->fili_banda) soffitto = proc->fili_banda - FILI_BANDA;

            proc->heap_max = (soffitto > PAGE_SIZE) ? soffitto - PAGE_SIZE : 0;
        }

        klog(LOG_INFO, "ELF: heap utente 0x%08x, tetto 0x%08x (%u MB)",
             heap_start, proc->heap_max,
             (proc->heap_max - heap_start) / (1024u * 1024u));
    }

    /* Successo */

    /* ! SI ASSEGNA, NON SI AGGIUNGE: un driver che esegue /bin/sh diventa
     * /bin/sh, e il varco verso l'hardware si chiude dietro di lui. Fosse un
     * OR, basterebbe un driver qualunque per fare da scala a un programma
     * qualunque.
     *
     * ! E si assegna QUI, a caricamento riuscito, non in cima alla funzione:
     * sys_exec su un errore rimette la vecchia page directory e il processo
     * prosegue con l'immagine di prima. Deciderlo prima di sapere se il
     * caricamento riesce vorrebbe dire togliere — o dare — il privilegio a un
     * processo che sta ancora eseguendo un altro programma. */
    if (proc != NULL) proc->is_driver = (uint32_t)percorso_di_driver(path);

    result->entry_point = hdr.e_entry;
    klog(LOG_INFO, "ELF: '%s' caricato con successo, entry=0x%08x", path, hdr.e_entry);
    ret = 0;

cleanup:
    /* L'handle si chiude SOLO se il caricamento e' residente o se e'
     * fallito. Su richiesta resta aperto per tutta la vita del processo:
     * e' l'unica sorgente da cui le pagine mancanti possono arrivare, e
     * chiuderlo qui vorrebbe dire un processo che si ferma al primo fault
     * con "handle non valido". Lo chiude proc_reap_zombie. */
    if (ret == 0 && !residente) {
        proc->exe_handle = handle;
    } else {
        vfs_close(handle);
        if (proc != NULL) proc->n_vma = 0;
    }
    if (phdrs)   kfree(phdrs);
    if (seg_buf) kfree(seg_buf);
    return ret;
}

/* Le due porte d'ingresso. Vedi il commento su elf_carica per quale usare
 * quando: i programmi normali su richiesta, i driver residenti. */
int elf_load(const char *path, Process *proc, ElfLoadResult *result)
{
    return elf_carica(path, proc, result, 0, 0);
}

int elf_load_argv(const char *path, Process *proc, ElfLoadResult *result,
                  uint32_t stack_extra)
{
    return elf_carica(path, proc, result, 0, stack_extra);
}

int elf_load_residente(const char *path, Process *proc, ElfLoadResult *result)
{
    return elf_carica(path, proc, result, 1, 0);
}

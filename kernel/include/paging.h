/* =============================================================================
 * kernel/include/paging.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef PAGING_H
#define PAGING_H

#include "kernel.h"
#include "idt.h"

typedef uint32_t PDE;
typedef uint32_t PTE;

/* Flag pagine */
#define PG_PRESENT      (1 << 0)
#define PG_WRITABLE     (1 << 1)
#define PG_USER         (1 << 2)
#define PG_WRITE_THRU   (1 << 3)
#define PG_CACHE_DIS    (1 << 4)
#define PG_ACCESSED     (1 << 5)
#define PG_DIRTY        (1 << 6)
#define PG_GLOBAL       (1 << 8)
/* =============================================================================
 * PG_RISERVA — una pagina RISERVATA ma senza memoria (kernel 0.230)
 *
 * Una PTE non presente con il bit 10 (uno dei tre che la CPU lascia al
 * sistema; il 9 e' PG_SWAP, vedi swap.h). La mette una mmap anonima PROT_NONE:
 * lo spazio e' del processo, ma nessuna pagina fisica c'e' ancora. Chi la
 * tocca muore come per ogni PROT_NONE; mprotect le da' una pagina azzerata
 * nel momento in cui la rende accessibile. Prima PROT_NONE allocava e azzerava
 * davvero ogni pagina: Gecko ne riserva 1 GB per cercare un buco e lo rende
 * subito, e su una macchina da 1,5 GB passava per lo swap.
 * ========================================================================== */
#define PG_RISERVA      (1u << 10)
#define PTE_E_RISERVA(pte)  (((pte) & (PG_PRESENT | (1u << 9) | PG_RISERVA)) == PG_RISERVA)

/* =============================================================================
 * FINESTRA DI RIMAPPATURA FISICA
 *
 * Una pagina virtuale sola, dentro la fascia mappata in OGNI page
 * directory, che il kernel ripunta alla pagina fisica che deve leggere o
 * scrivere. Serve per toccare memoria di un ALTRO spazio di
 * indirizzamento — le pagine di un processo che si sta creando, o quelle
 * appena allocate a chi ha chiamato sbrk — senza dipendere dal fatto che
 * quell'indirizzo fisico sia mappato nel CR3 corrente. Vedi paging.c per
 * il perche' e per le due regole d'uso.
 *
 * paging_finestra_apri ritorna un puntatore all'indirizzo fisico chiesto
 * (offset dentro la pagina compreso) e DISABILITA gli interrupt fino
 * alla chiusura: la finestra e' una risorsa sola.
 * ============================================================================= */
#define PAGING_FINESTRA_VIRT    0x003FF000u  /* ultima pagina dei primi 4 MB */
#define PAGING_FINESTRA_FISICA  0x003FF000u  /* la sua identita', riservata */

/* Porta in RAM le pagine dell'eseguibile che coprono un buffer utente,
 * prima di consegnarlo a un driver. Vedi paging.c. */
void     vm_precarica_utente(uint32_t addr, uint32_t len);

void    *paging_finestra_apri(uint32_t phys);
void     paging_finestra_chiudi(void);
void     paging_azzera_fisica(uint32_t phys);

void     paging_init(void);

/* 1 se la fascia kernel e' descritta con pagine da 4 MB (CR4.PSE acceso).
 * Lo si puo' chiedere per dirlo in un log: non cambia come si mappa niente,
 * perche' paging_map_page spezza da sola il blocco che le si para davanti. */
int      paging_pse_attivo(void);

/* Mappa il framebuffer VESA con identita' nella PD del kernel E lo annota,
 * cosi' ogni PD di processo creata dopo se lo ritrova. Vedi paging.c. */
int      paging_mappa_framebuffer(uint32_t phys, uint32_t byte);
int      paging_map_page(PDE *pd, uint32_t virt, uint32_t phys, uint32_t flags);
void     paging_unmap_page(PDE *pd, uint32_t virt);
uint32_t paging_get_physical(PDE *pd, uint32_t virt);
int      paging_riserva(PDE *pd, uint32_t virt);
PDE     *paging_create_directory(void);
void     paging_destroy_directory(PDE *pd);

/* =============================================================================
 * Le due meta' dello sfratto che riguardano le TABELLE, non il disco
 *
 * ! STANNO QUI E NON IN swap.c PERCHE' LE MACRO CHE SPEZZANO UN INDIRIZZO IN
 * INDICI SONO PRIVATE DI paging.c, ed e' giusto che lo siano: due copie degli
 * stessi turni di bit sono due modi di sbagliarli. swap.c decide cosa farne,
 * qui si guarda e si scrive.
 *
 * paging_vittima  cerca una pagina utente che si possa mandare via e rende 1,
 *                 riempiendo i tre argomenti; 0 se non ce n'e' nessuna.
 * paging_marca_swap  sostituisce la mappatura con il segnaposto dello slot,
 *                 conservando i permessi che la pagina aveva.
 * ========================================================================== */
int      paging_vittima(PDE **out_pd, uint32_t *out_virt, uint32_t *out_frame);
void     paging_marca_swap(PDE *pd, uint32_t virt, uint32_t slot);
void     paging_switch(PDE *pd);
PDE     *paging_get_kernel_directory(void);
PDE     *paging_get_current_directory(void);
void     page_fault_handler(InterruptFrame *frame);

/* mprotect(): PROT_* on `pagine` pages from `virt` of process `p` (a
 * struct Process *). 0, -ENOMEM (a page that does not exist), -EACCES (write
 * on a frame shared with other processes). See paging.c. */
struct Process;
int      paging_proteggi(struct Process *p, uint32_t virt, uint32_t pagine,
                         uint32_t prot);
/* 1 if the kernel may write [va, va+len) for process p: present (brought in
 * if it has to be), user and writable. For the signal frame. See paging.c. */
int      paging_utente_pronta(struct Process *p, uint32_t va, uint32_t len);

#endif /* PAGING_H */

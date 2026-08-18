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
PDE     *paging_create_directory(void);
void     paging_destroy_directory(PDE *pd);
void     paging_switch(PDE *pd);
PDE     *paging_get_kernel_directory(void);
PDE     *paging_get_current_directory(void);
void     page_fault_handler(InterruptFrame *frame);

#endif /* PAGING_H */

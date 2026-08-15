/* =============================================================================
 * kernel/include/pmm.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef PMM_H
#define PMM_H

#include "kernel.h"

#define PAGE_SIZE   4096

void     pmm_init(BootInfo *info);
uint32_t pmm_alloc_page(void);
uint32_t pmm_alloc_pages(uint32_t count);

/* Allocazione nella FASCIA KERNEL (sotto USER_SPACE_BASE): obbligatoria per
 * tutto cio' che il kernel raggiunge al proprio indirizzo FISICO — heap di
 * kmalloc, stack kernel, page directory e page table. Vedi il commento in
 * pmm.c. Per le pagine dell'utente si usa pmm_alloc_page(), che pesca da
 * tutta la RAM. */
uint32_t pmm_alloc_page_kernel(void);
uint32_t pmm_alloc_pages_kernel(uint32_t count);
void     pmm_free_page(uint32_t addr);
void     pmm_free_pages(uint32_t addr, uint32_t count);
/* =============================================================================
 * CONTEGGIO DEI RIFERIMENTI — una pagina fisica con piu' di un proprietario
 *
 * Fino ad agosto 2026 ogni pagina mappata nello spazio utente di un processo
 * era ESCLUSIVAMENTE sua, e paging_destroy_directory() poteva liberare tutto
 * cio' che trovava. Con la memoria condivisa non e' piu' vero: la stessa
 * pagina fisica sta nella tabella di due processi, e il primo che muore non
 * deve portarsela via.
 *
 * ! IL CONTEGGIO STA QUI E NON NEL GESTORE DELLE ZONE, ed e' la decisione che
 * conta. Nel PMM copre OGNI strada che libera una pagina — munmap, la morte
 * del processo, un errore a meta' di elf_load — senza che nessuna di quelle
 * sappia che esiste la condivisione. Un conteggio tenuto piu' in alto avrebbe
 * richiesto di correggere ogni sito di liberazione, e ne basta uno dimenticato
 * perche' il guasto sia una pagina riusata mentre qualcuno ci scrive dentro.
 *
 * ! IL VALORE MEMORIZZATO E' «QUANTI IN PIU' DEL PRIMO»: zero vuol dire un
 * solo proprietario, cioe' il caso normale. Cosi' allocare non deve toccare
 * niente e l'array nasce azzerato con il significato giusto.
 *
 * pmm_ref_inc  aggiunge un proprietario. Rende 0, o <0 se la pagina non e' RAM
 *              (una finestra MMIO non si conta: non e' nostra) o se si
 *              sfonderebbe il tetto di 255 proprietari.
 * pmm_ref_count rende quanti sono, 1 per una pagina normale. Per la
 *              diagnostica e per le prove.
 *
 * Chi libera NON deve sapere niente: pmm_free_page() cala il conteggio e
 * restituisce la pagina solo quando arriva all'ultimo proprietario.
 * ============================================================================= */
int      pmm_ref_inc(uint32_t addr);
uint32_t pmm_ref_count(uint32_t addr);

uint32_t pmm_get_free_pages(void);
uint32_t pmm_get_used_pages(void);
uint32_t pmm_get_total_pages(void);
int      pmm_is_page_free(uint32_t addr);
void     pmm_dump(void);

/* =============================================================================
 * pmm_region_stat — quante pagine, di cui libere, in [base, base+len)
 *
 * Serve a SYS_MEMINFO per dare i totali per fascia (convenzionale, area
 * superiore, estesa) invece del solo totale globale. Conta interrogando la
 * bitmap pagina per pagina: la fascia piu' grande su una macchina da 192MB
 * sono ~48000 iterazioni, irrilevanti per un comando interattivo e senza
 * alcuno stato da mantenere aggiornato in tempo reale.
 *
 * Le pagine oltre la RAM realmente presente non vengono contate: una
 * fascia interamente fuori dalla memoria installata risulta di zero
 * pagine, che e' esattamente cio' che si vuole mostrare.
 *
 * out_total e out_free possono essere NULL.
 * ============================================================================= */
void     pmm_region_stat(uint32_t base, uint32_t len,
                         uint32_t *out_total, uint32_t *out_free);

#endif /* PMM_H */

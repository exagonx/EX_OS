/* =============================================================================
 * tools/prove64/memoria-alta.c — does a process get and keep a lot of memory?
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     memoria-alta [MB]        (default 3500)
 *
 * @EXOS64-SPAZIO: takes MB megabytes in 1 MB blocks with malloc, writes in
 * every 4 KB page a value made from its block and page, then reads them all
 * back. Run in QEMU with more than 4 GB of RAM, part of it is RAM above 4 GB
 * (the high pool of kernel/mm/pmm.c); a page given twice, or a frame cut to
 * 32 bits somewhere, shows up as a wrong value. Prints the memory the system
 * reports before and after (the `mem` numbers).
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define MB (1024u * 1024u)

int main(int argc, char **argv)
{
    unsigned int mb = argc > 1 ? (unsigned int)atoi(argv[1]) : 3500u;
    unsigned char **blocchi = calloc(mb, sizeof(*blocchi));
    unsigned int i, j, presi = 0, sbagliati = 0;

    if (!blocchi) { printf("memoria-alta: calloc fallita\n"); return 1; }
    printf("memoria-alta: chiedo %u MB a blocchi di 1 MB\n", mb);
    for (i = 0; i < mb; i++) {
        blocchi[i] = malloc(MB);
        if (!blocchi[i]) break;
        for (j = 0; j < MB; j += 4096)
            *(uint64_t *)(blocchi[i] + j) = ((uint64_t)i << 32) | j | 0x5A000000u;
        presi++;
        if (presi % 500 == 0) printf("  %u MB presi e scritti\n", presi);
    }
    printf("memoria-alta: %u MB presi\n", presi);
    for (i = 0; i < presi; i++)
        for (j = 0; j < MB; j += 4096)
            if (*(uint64_t *)(blocchi[i] + j) != (((uint64_t)i << 32) | j | 0x5A000000u))
                sbagliati++;
    printf("memoria-alta: riletti %u MB, pagine sbagliate: %u\n", presi, sbagliati);
    printf("%s\n", sbagliati == 0 && presi == mb ? "MEMORIA-ALTA-OK" : "MEMORIA-ALTA-NO");
    return sbagliati != 0;
}

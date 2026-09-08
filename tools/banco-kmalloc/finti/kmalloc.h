#ifndef FINTO_KMALLOC_H
#define FINTO_KMALLOC_H
#include <stddef.h>
void  kmalloc_init(void);
void *kmalloc(size_t size);
void  kfree(void *ptr);
void *kmalloc_aligned(size_t size, size_t alignment);
void  kfree_aligned(void *ptr);
void  kmalloc_stats(void);
uint32_t kmalloc_verifica(void);
#endif

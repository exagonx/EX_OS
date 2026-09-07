/* Header finto: il minimo che kmalloc.c chiede al kernel, per provarlo
 * sull'ospite. Vedi banco.c. */
#ifndef FINTO_KERNEL_H
#define FINTO_KERNEL_H
#include <stdint.h>
#include <stddef.h>
#define PAGE_SIZE 4096u
#define KERNEL_HEAP_BASE 0x00400000u
#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };
void klog(int liv, const char *fmt, ...);
void kpanic(const char *fmt, ...);
#endif

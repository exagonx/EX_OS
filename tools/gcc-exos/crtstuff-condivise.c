/* =============================================================================
 * tools/gcc-exos/crtstuff-condivise.c — crtbeginS.o and crtendS.o for EX-OS
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     -DINIZIO -fPIC -c  ->  crtbeginS.o   (first object of every `gcc -shared`)
 *     -DFINE   -fPIC -c  ->  crtendS.o     (last one)
 * Built and installed by tools/gcc-exos/specs-condivise.sh.
 *
 * ! WITHOUT THEM AN EXCEPTION CANNOT LEAVE A FUNCTION OF A SHARED OBJECT
 * (16 October 2026, @RUST-64 step b: rustc loads procedural macros with
 * dlopen, and a macro reports its errors by panicking). The EX-OS libgcc is
 * built with inhibit_libc (cross64.sh: --without-headers), so its unwinder
 * never asks dl_iterate_phdr where the unwind tables are: it knows only the
 * tables someone registered with __register_frame_info. In a program
 * crtbegin.o does that; a shared object linked its own copy of the unwinder
 * (libgcc.a is static) and nobody registered anything in it - the first
 * panic found no frame description and aborted.
 *
 * As GCC's crtstuff.c does: INIZIO puts an empty .eh_frame piece first, so
 * its label is where the library's .eh_frame begins; FINE puts the zero word
 * that ends the list last. A constructor registers the tables with the
 * library's OWN unwinder, a destructor (dlclose) removes them. The references
 * are weak: a library with no unwinder in it (no C++ exceptions, no Rust
 * panics) does not pull one in, and then there is nothing to register.
 * ============================================================================= */
#if defined(INIZIO)

static const char inizio_eh_frame[]
    __attribute__((section(".eh_frame"), aligned(4), used)) = { };

/* struct object of unwind-dw2-fde.h: six pointer-sized words; more is fine. */
static struct { void *parole[8]; } oggetto;

extern void  __register_frame_info(const void *inizio, void *oggetto) __attribute__((weak));
extern void *__deregister_frame_info(const void *inizio) __attribute__((weak));

static void __attribute__((constructor(101))) registra_tabelle(void)
{
    if (__register_frame_info)
        __register_frame_info(inizio_eh_frame, &oggetto);
}

static void __attribute__((destructor(101))) cancella_tabelle(void)
{
    if (__deregister_frame_info)
        __deregister_frame_info(inizio_eh_frame);
}

#elif defined(FINE)

/* The zero length that ends the .eh_frame list. */
static const unsigned int fine_eh_frame[]
    __attribute__((section(".eh_frame"), aligned(4), used)) = { 0 };

#else
#error "build with -DINIZIO or -DFINE"
#endif

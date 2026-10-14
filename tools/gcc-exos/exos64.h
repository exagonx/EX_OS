/* Definitions for the x86_64-exos target (EX-OS, 64-bit).
   Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* The 64-bit twin of exos.h (@EXOS-64 in the EX-OS tree, 2026-10-14).

   Same system, same rules: a plain ELF target with the system's own libc,
   statically linked, programs loaded at 0x08000000.  What changes is the
   ABI, which is the standard x86-64 one: LP64, arguments in registers,
   the red zone, the thread pointer in %fs.  The EX-OS kernel and libc
   were written for exactly that (see lib/start.S and lib/libc.c).

   Two choices differ from the 32-bit target on purpose:
     - the unwind tables stay.  At 32 bit they were dropped to save space
       on a 1.44 MB floppy; the 64-bit system has no floppy, and C++
       exceptions and Rust's panics need them;
     - the fundamental types are x86_64-linux's, so that C++ name mangling
       and every "%ld" agree with the rest of the world.  */

#undef  TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__exos__");		\
      builtin_define ("__EXOS__");		\
      builtin_define ("__ELF__");		\
      builtin_assert ("system=exos");		\
    }						\
  while (0)

/* Static only: EX-OS has no dynamic linker for programs.  The address
   is the one of lib/programma64.ld, so that a program built here and one
   built by the system Makefile can be compared instruction by
   instruction.  */
#undef  LINK_SPEC
#define LINK_SPEC "-m elf_x86_64 -static -e _start -Ttext-segment=0x08000000 %{shared:%eEX-OS does not support shared libraries for programs}"

/* lib/start.S of EX-OS, installed as crt0.o, then crtbegin.o; crtend.o at
   the end.  ! THE crtbegin/crtend PAIR IS NOT OPTIONAL HERE: they register
   the program's .eh_frame with the unwinder (frame_dummy, run from
   .init_array by the libc's start-up) and terminate it.  Without them the
   first C++ throw found no frame description even for itself and stopped
   on the ud2 of gcc_unreachable() in uw_init_context_1 (2026-10-14).  The
   32-bit target does not need them: it uses setjmp/longjmp exceptions.  */
#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "crt0%O%s crtbegin%O%s"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC "crtend%O%s"

#undef  LIB_SPEC
#define LIB_SPEC "-lc"

/* Programs are not position independent unless asked: the loader puts
   them at their link address.  */
#undef  CC1_SPEC
#define CC1_SPEC "%{!fpic:%{!fPIC:%{!fpie:%{!fPIE:-fno-pic}}}}"

/* A program's stack is not executable.  */
#undef  TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END file_end_indicate_exec_stack

/* The fundamental types, as on x86_64-linux.  They must come after
   newlib-stdint.h in tm_file, and they do: exos64.h is the last one (see
   the x86_64-*-exos* case in config.gcc).  */
#undef  SIZE_TYPE
#define SIZE_TYPE      "long unsigned int"
#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE   "long int"
#undef  WCHAR_TYPE
#define WCHAR_TYPE     "int"
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32
#undef  INT32_TYPE
#define INT32_TYPE     "int"
#undef  UINT32_TYPE
#define UINT32_TYPE    "unsigned int"
#undef  INT64_TYPE
#define INT64_TYPE     "long int"
#undef  UINT64_TYPE
#define UINT64_TYPE    "long unsigned int"
#undef  INTMAX_TYPE
#define INTMAX_TYPE    "long int"
#undef  UINTMAX_TYPE
#define UINTMAX_TYPE   "long unsigned int"

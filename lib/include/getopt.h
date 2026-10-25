/* =============================================================================
 * lib/include/getopt.h — getopt, getopt_long, getopt_long_only
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * In libc.a only (lib/libc_getopt.c): POSIX getopt as in <unistd.h>, and the
 * long options of GNU and BSD. Options stop at the first operand, as POSIX
 * says: no reordering of argv.
 * ============================================================================= */
#ifndef EXOS_GETOPT_H
#define EXOS_GETOPT_H

#ifdef __cplusplus
extern "C" {
#endif

extern char *optarg;
extern int   optind, opterr, optopt;

int getopt(int argc, char *const argv[], const char *optstring);

struct option {
    const char *name;
    int         has_arg;        /* no_argument, required_argument, optional_argument */
    int        *flag;           /* not NULL: *flag = val and 0 is returned */
    int         val;
};

#define no_argument        0
#define required_argument  1
#define optional_argument  2

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex);
int getopt_long_only(int argc, char *const argv[], const char *optstring,
                     const struct option *longopts, int *longindex);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_GETOPT_H */

/* =============================================================================
 * lib/libc_getopt.c — getopt, getopt_long (included by lib/libc.c)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * POSIX getopt, and the getopt_long / getopt_long_only that GNU and BSD
 * libcs have and almost every ported program uses. ncurses' configure stops
 * without getopt ("getopt is required for building programs": tic, infocmp).
 *
 * As POSIX says, and as musl does: options stop at the first operand (no
 * GNU reordering of argv), "--" ends them, a leading ':' in optstring makes
 * a missing argument return ':' instead of '?' and keeps quiet, opterr = 0
 * keeps quiet too. optind = 0 starts again from the beginning (GNU), as 1.
 *
 * Only in libc.a (lib/libc.c includes it under !EXOS_LIBC_SO): the shared
 * libc exports no variables, and optarg/optind are variables.
 * ============================================================================= */

/* the type of lib/include/getopt.h, which libc.c does not include */
#ifndef EXOS_GETOPT_H
struct option {
    const char *name;
    int         has_arg;
    int        *flag;
    int         val;
};
#endif

char *optarg;
int   optind = 1, opterr = 1, optopt;
static int g_optpos;            /* inside a group of short options: -abc */

static void getopt_dice(const char *argv0, const char *cosa, const char *opt, int lun)
{
    if (!opterr) return;
    fprintf(stderr, "%s: %s: ", argv0, cosa);
    fwrite(opt, 1, (size_t)lun, stderr);
    fputc('\n', stderr);
}

int getopt(int argc, char *const argv[], const char *optstring)
{
    const char *p, *a;
    int c, zitto = (optstring[0] == ':');

    if (optind == 0) { optind = 1; g_optpos = 0; }
    optarg = 0;
    if (optind >= argc || argv[optind] == 0) return -1;
    a = argv[optind];
    if (g_optpos == 0) {
        if (a[0] != '-' || a[1] == '\0') return -1;
        if (a[1] == '-' && a[2] == '\0') { optind++; return -1; }
        g_optpos = 1;
    }

    c = (unsigned char)a[g_optpos++];
    p = (c == ':') ? 0 : strchr(optstring + zitto, c);
    if (a[g_optpos] == '\0') { optind++; g_optpos = 0; }

    if (p == 0) {
        char ch = (char)c;
        optopt = c;
        if (!zitto) getopt_dice(argv[0], "unrecognized option", &ch, 1);
        return '?';
    }
    if (p[1] == ':') {
        if (g_optpos) {                         /* -ovalue */
            optarg = (char *)a + g_optpos;
            optind++; g_optpos = 0;
        } else if (p[2] == ':') {               /* optional: only -ovalue */
            optarg = 0;
        } else if (optind < argc) {             /* -o value */
            optarg = argv[optind++];
        } else {
            char ch = (char)c;
            optopt = c;
            if (zitto) return ':';
            getopt_dice(argv[0], "option requires an argument", &ch, 1);
            return '?';
        }
    }
    return c;
}

static int getopt_lunga(int argc, char *const argv[], const char *optstring,
                        const struct option *lo, int *indice, int sola)
{
    const char *a, *nome, *uguale;
    int i, trovata = -1, ambigua = 0, zitto = (optstring[0] == ':');
    size_t lun;

    if (optind == 0) { optind = 1; g_optpos = 0; }
    if (g_optpos != 0 || optind >= argc || argv[optind] == 0)
        return getopt(argc, argv, optstring);
    a = argv[optind];
    if (a[0] != '-' || a[1] == '\0') return -1;
    if (a[1] == '-' && a[2] == '\0') { optind++; return -1; }
    if (a[1] != '-' && !sola) return getopt(argc, argv, optstring);

    nome   = a + (a[1] == '-' ? 2 : 1);
    uguale = strchr(nome, '=');
    lun    = uguale ? (size_t)(uguale - nome) : strlen(nome);

    for (i = 0; lo[i].name; i++) {
        if (strncmp(lo[i].name, nome, lun) != 0) continue;
        if (lo[i].name[lun] == '\0') { trovata = i; ambigua = 0; break; }
        if (trovata < 0) trovata = i;           /* a prefix: unique or not */
        else if (lo[trovata].has_arg != lo[i].has_arg || lo[trovata].flag != lo[i].flag ||
                 lo[trovata].val != lo[i].val) ambigua = 1;
    }
    /* getopt_long_only: "-x" that is no long option is a short one */
    if (trovata < 0 && sola && a[1] != '-' && strchr(optstring + zitto, a[1]))
        return getopt(argc, argv, optstring);

    optind++;
    optarg = 0;
    if (trovata < 0 || ambigua) {
        optopt = 0;
        if (!zitto) getopt_dice(argv[0], ambigua ? "ambiguous option" : "unrecognized option",
                                a, (int)strlen(a));
        return '?';
    }
    if (lo[trovata].has_arg == 0 && uguale) {
        optopt = lo[trovata].val;
        if (!zitto) getopt_dice(argv[0], "option takes no argument", a, (int)strlen(a));
        return '?';
    }
    if (lo[trovata].has_arg != 0) {
        if (uguale)                                   optarg = (char *)uguale + 1;
        else if (lo[trovata].has_arg == 1 && optind < argc) optarg = argv[optind++];
        else if (lo[trovata].has_arg == 1) {
            optopt = lo[trovata].val;
            if (zitto) return ':';
            getopt_dice(argv[0], "option requires an argument", a, (int)strlen(a));
            return '?';
        }
    }
    if (indice) *indice = trovata;
    if (lo[trovata].flag) { *lo[trovata].flag = lo[trovata].val; return 0; }
    return lo[trovata].val;
}

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *lo, int *indice)
{
    return getopt_lunga(argc, argv, optstring, lo, indice, 0);
}

int getopt_long_only(int argc, char *const argv[], const char *optstring,
                     const struct option *lo, int *indice)
{
    return getopt_lunga(argc, argv, optstring, lo, indice, 1);
}

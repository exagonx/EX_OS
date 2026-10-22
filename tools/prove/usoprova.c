/* =============================================================================
 * tools/prove/usoprova.c — hold a mount busy, for tools/prova_uso.sh
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     usoprova FILE SCRIPT &
 *
 * Keeps FILE open (and its own program, if it sits on the same mount) and
 * sleeps forever. SCRIPT gets one line, "kill <pid>": the test cannot read
 * a pid back from the screen, so the shell runs the script to kill this
 * process with /bin/kill. SCRIPT goes on another mount (/rdboot/tmp), or
 * running it would hold the mount busy again.
 * ============================================================================= */
#include <libc.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    char riga[32];
    int  fd, s, n;

    if (argc < 3) { printf("uso: usoprova FILE SCRIPT\n"); return 2; }
    fd = open(argv[1], O_RDWR | O_CREAT);
    if (fd < 0) { printf("USO non apro %s\n", argv[1]); return 1; }

    n = snprintf(riga, sizeof(riga), "kill %d\n", getpid());
    s = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (s < 0 || write(s, riga, (unsigned int)n) != n) { printf("USO non scrivo %s\n", argv[2]); return 1; }
    close(s);
    printf("USO pid %d tiene %s\n", getpid(), argv[1]);

    for (;;) sleep(1);
}

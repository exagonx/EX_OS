/* =============================================================================
 * tools/prove/floppyprova.c - two handles of one floppy file are one file
 * (analisi voce 70)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     floppyprova <dir on the FAT12 floppy>
 *
 *   uno: A writes 3000 bytes, B (opened before) writes 1 byte at 3000:
 *        the file is 3001 bytes and A's bytes are there;
 *   due: a file deleted while open still reads whole, also after a new file
 *        has taken free clusters;
 *   tre: a file renamed while open and written again keeps the new name.
 * Then tools/prova_floppy.sh runs fsck.fat on the image: no lost clusters.
 * Prints "FLOPPY FINE N".
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static char buf[4096], rilett[4096];

static int uno(const char *d)
{
    char p[256];
    int  a, b, k, ok = 1;
    struct stat st;

    snprintf(p, sizeof(p), "%s/UNO.BIN", d);
    a = open(p, O_RDWR | O_CREAT | O_TRUNC, 0644);
    b = open(p, O_RDWR);
    for (k = 0; k < 3000; k++) buf[k] = (char)('a' + k % 26);
    if (a < 0 || b < 0 || write(a, buf, 3000) != 3000 || pwrite(b, "Z", 1, 3000) != 1) ok = 0;
    close(a); close(b);
    if (stat(p, &st) != 0 || st.st_size != 3001) ok = 0;
    a = open(p, O_RDONLY);
    if (a < 0 || read(a, rilett, 3001) != 3001 || memcmp(rilett, buf, 3000) != 0 || rilett[3000] != 'Z') ok = 0;
    close(a);
    printf("FLOPPY uno: %s (misura %ld)\n", ok ? "OK" : "NO", (long)st.st_size);
    return !ok;
}

static int due(const char *d)
{
    char p[256], q[256];
    int  a, c, k, ok = 1;

    snprintf(p, sizeof(p), "%s/DUE.BIN", d);
    snprintf(q, sizeof(q), "%s/ALTRO.BIN", d);
    a = open(p, O_RDWR | O_CREAT | O_TRUNC, 0644);
    for (k = 0; k < 2500; k++) buf[k] = (char)('A' + k % 26);
    if (a < 0 || write(a, buf, 2500) != 2500) ok = 0;
    if (unlink(p) != 0) ok = 0;
    c = open(q, O_RDWR | O_CREAT | O_TRUNC, 0644);
    memset(rilett, 'q', 2500);
    if (c < 0 || write(c, rilett, 2500) != 2500) ok = 0;
    close(c);
    memset(rilett, 0, sizeof(rilett));
    if (pread(a, rilett, 2500, 0) != 2500 || memcmp(rilett, buf, 2500) != 0) ok = 0;
    close(a);
    if (access(p, F_OK) == 0) ok = 0;
    printf("FLOPPY due: %s\n", ok ? "OK" : "NO");
    return !ok;
}

static int tre(const char *d)
{
    char p[256], q[256];
    int  a, ok = 1;
    struct stat st;

    snprintf(p, sizeof(p), "%s/TRE.BIN", d);
    snprintf(q, sizeof(q), "%s/NUOVO.BIN", d);
    unlink(q);
    a = open(p, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (a < 0 || write(a, "0123456789", 10) != 10) ok = 0;
    if (rename(p, q) != 0) ok = 0;
    if (write(a, "abcdefghij", 10) != 10) ok = 0;
    close(a);
    if (stat(q, &st) != 0 || st.st_size != 20) ok = 0;
    if (access(p, F_OK) == 0) ok = 0;
    printf("FLOPPY tre: %s\n", ok ? "OK" : "NO");
    return !ok;
}

int main(int argc, char **argv)
{
    int no;

    if (argc < 2) { printf("uso: floppyprova DIR\n"); return 2; }
    no = uno(argv[1]) + due(argv[1]) + tre(argv[1]);
    sync();
    printf("FLOPPY FINE %d\n", no);
    return no ? 1 : 0;
}

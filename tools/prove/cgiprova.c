/* =============================================================================
 * tools/prove/cgiprova.c - a CGI program for tools/prova_httpd.sh
 * EX-OS - Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Started by Apache's mod_cgi through APR (apr_proc_create, which on EX-OS
 * is spawn_ex: tools/apr-exos/patch/proc-spawn.diff). It answers with what
 * it got: the method, the query, the body read from stdin, and the
 * directory it was started in (mod_cgi asks for the script's own).
 *
 *     CGI ok metodo=<method> query=<query> corpo=<body> cartella=<cwd>
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char *metodo = getenv("REQUEST_METHOD");
    const char *query  = getenv("QUERY_STRING");
    const char *lung   = getenv("CONTENT_LENGTH");
    char        corpo[256] = "", cartella[256] = "?";
    int         n = lung ? atoi(lung) : 0, presi = 0, q;

    if (n > (int)sizeof(corpo) - 1) n = (int)sizeof(corpo) - 1;
    while (presi < n && (q = (int)read(0, corpo + presi, (size_t)(n - presi))) > 0)
        presi += q;
    corpo[presi] = '\0';
    if (getcwd(cartella, sizeof(cartella)) == NULL) strcpy(cartella, "?");
    printf("Content-Type: text/plain\r\n\r\n");
    printf("CGI ok metodo=%s query=%s corpo=%s cartella=%s\n",
           metodo ? metodo : "-", query ? query : "-", corpo, cartella);
    return 0;
}

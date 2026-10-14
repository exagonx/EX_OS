/* =============================================================================
 * tools/prova-dl/prova-js.c — the JavaScript engines as shared objects (64 bit)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     prova-js [1]       1 asks for QuickJS, nothing for ExJs
 *
 * Linked like an EX-OS command at 64 bit (the archive with the ExJs stub):
 * the stub opens /exwin/lib/exjs.so or quickjs.so with dlopen and the same
 * script runs on the engine that answered. Built by tools/prova-dl/prova-js.sh.
 * ============================================================================= */
#include "libc.h"
#include "exjs.h"

int main(int argc, char **argv)
{
    static const char *script =
        "var a = [3, 1, 2].map(function (x) { return x * 2; }).sort().join('-');"
        "a + ' ' + Math.round(Math.sqrt(2) * 1000) + ' ' + JSON.stringify({k: [1, 'x']}) +"
        "' ' + /b(c+)/.exec('abccd')[1] + ' ' + (0.1 + 0.2).toFixed(2);";
    const char *atteso = "2-4-6 1414 {\"k\":[1,\"x\"]} cc 0.30";
    unsigned int quanto;
    void        *mem;
    ExJsCtx     *c;
    ExJsVal      v;
    ExJsErrore   e;
    const char  *s;

    exjs_motore(argc > 1 && argv[1][0] == '1');
    quanto = exjs_quanto_serve(20000, 1u << 20);
    mem = malloc(quanto);
    c = mem ? exjs_apri(mem, quanto, 20000, 1u << 20) : 0;
    if (!c) { printf("prova-js: no context\n"); return 1; }
    printf("prova-js: engine %s\n", exjs_motore_ora() == 1 ? "QuickJS (quickjs.so)" : "ExJs (exjs.so)");
    if (!exjs_esegui(c, script, (unsigned int)strlen(script), &v, &e)) {   /* 1 = done */
        printf("prova-js: error at line %d: %s\n", e.riga, e.messaggio);
        return 1;
    }
    s = exjs_a_stringa(c, v);
    printf("prova-js: %s\n", s ? s : "(null)");
    printf("prova-js: %s\n", s && strcmp(s, atteso) == 0 ? "OK" : "NO");
    exjs_chiudi(c);
    return 0;
}

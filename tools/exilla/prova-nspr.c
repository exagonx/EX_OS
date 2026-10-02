/* =============================================================================
 * tools/exilla/prova-nspr.c — NSPR dentro EX-OS (tappa 5 di Exilla)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Si esegue con tools/exilla/prova-nspr.sh, con la rete di QEMU accesa e
 * dall'altra parte tools/exilla/prova-socket-host.py (eco TCP sulla 7801
 * dell'host, 10.0.0.2 visto da qui). NSPR e' quella dell'albero di Firefox,
 * costruita da mozbuild (tools/exilla/js-costruisci.sh, --enable-nspr-build).
 * Ogni controllo stampa [OK] o [NO]; l'ultima riga dice quanti NO.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include "nspr.h"

static int g_no = 0;

static void ok(const char *cosa, int va)
{
    printf("  %s  %s\n", va ? "[OK]" : "[NO]", cosa);
    if (!va) g_no++;
}

/* --- fili, lucchetto, condizione ------------------------------------------- */
static PRLock    *g_lock;
static PRCondVar *g_cv;
static int        g_conto, g_pronti;

static void lavora(void *arg)
{
    int i;

    (void)arg;
    for (i = 0; i < 10000; i++) {
        PR_Lock(g_lock);
        g_conto++;
        PR_Unlock(g_lock);
    }
    PR_Lock(g_lock);
    g_pronti++;
    PR_NotifyCondVar(g_cv);
    PR_Unlock(g_lock);
}

int main(void)
{
    PRThread *t[4];
    int       i;

    PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 0);
    printf("prova-nspr: NSPR %s dentro EX-OS\n", PR_GetVersion());

    {
        char nome[64] = "";
        PR_GetSystemInfo(PR_SI_SYSNAME, nome, sizeof(nome));
        /* Su Unix NSPR lo chiede a uname(), che risponde come il comando. */
        printf("        (PR_SI_SYSNAME: %s)\n", nome);
        ok("PR_GetSystemInfo: il sistema si chiama EX-OS", strcmp(nome, "EX-OS") == 0);
        ok("un processore", PR_GetNumberOfProcessors() == 1);
    }

    g_lock = PR_NewLock();
    g_cv = PR_NewCondVar(g_lock);
    for (i = 0; i < 4; i++)
        t[i] = PR_CreateThread(PR_USER_THREAD, lavora, NULL, PR_PRIORITY_NORMAL,
                               PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
    PR_Lock(g_lock);
    while (g_pronti < 4) PR_WaitCondVar(g_cv, PR_MillisecondsToInterval(5000));
    PR_Unlock(g_lock);
    for (i = 0; i < 4; i++) PR_JoinThread(t[i]);
    ok("quattro fili, PRLock conteso, PRCondVar: 40000", g_conto == 40000);

    {
        PRIntervalTime a = PR_IntervalNow();
        PR_Sleep(PR_MillisecondsToInterval(250));
        PRUint32 ms = PR_IntervalToMilliseconds(PR_IntervalNow() - a);
        printf("        (PR_Sleep di 250 ms: %u ms)\n", ms);
        ok("PR_Sleep e PR_IntervalNow", ms >= 240 && ms < 1000);
    }

    {
        PRExplodedTime e;
        PR_ExplodeTime(PR_Now(), PR_GMTParameters, &e);
        ok("PR_Now e PR_ExplodeTime: l'anno e' plausibile", e.tm_year >= 2024 && e.tm_year < 2100);
    }

    {
        char buf[32];
        PR_SetEnv("PROVA_NSPR=si");
        ok("PR_SetEnv e PR_GetEnv", PR_GetEnv("PROVA_NSPR") && strcmp(PR_GetEnv("PROVA_NSPR"), "si") == 0);
        ok("PR_GetRandomNoise da' qualcosa", PR_GetRandomNoise(buf, sizeof(buf)) > 0);
    }

    {
        PRFileDesc *f = PR_Open("/disk/nspr.txt", PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE, 0644);
        char        letto[32] = "";
        PRFileInfo  info;
        int         scritto = f ? PR_Write(f, "ciao da NSPR", 12) : -1;

        if (f) PR_Close(f);
        f = PR_Open("/disk/nspr.txt", PR_RDONLY, 0);
        if (f) { PR_Read(f, letto, sizeof(letto) - 1); PR_Close(f); }
        ok("PR_Open, PR_Write, PR_Read", scritto == 12 && strcmp(letto, "ciao da NSPR") == 0);
        ok("PR_GetFileInfo: 12 byte, un file",
           PR_GetFileInfo("/disk/nspr.txt", &info) == PR_SUCCESS && info.size == 12 &&
           info.type == PR_FILE_FILE);
        ok("PR_Delete", PR_Delete("/disk/nspr.txt") == PR_SUCCESS &&
                        PR_Access("/disk/nspr.txt", PR_ACCESS_EXISTS) == PR_FAILURE);
    }

    {
        PRAddrInfo *ai = PR_GetAddrInfoByName("10.0.0.2", PR_AF_UNSPEC, PR_AI_ADDRCONFIG);
        PRNetAddr   a;
        void       *it = NULL;
        char        testo[32] = "";

        ok("PR_GetAddrInfoByName numerico", ai != NULL);
        if (ai) {
            it = PR_EnumerateAddrInfo(NULL, ai, 7801, &a);
            PR_NetAddrToString(&a, testo, sizeof(testo));
            ok("PR_EnumerateAddrInfo e PR_NetAddrToString: 10.0.0.2", it && strcmp(testo, "10.0.0.2") == 0);
            PR_FreeAddrInfo(ai);
        }

        {
            PRFileDesc *s = PR_OpenTCPSocket(PR_AF_INET);
            char        eco[64] = "";
            PRPollDesc  p;
            int         n;

            ok("PR_OpenTCPSocket e PR_Connect all'eco dell'host",
               s && PR_Connect(s, &a, PR_SecondsToInterval(10)) == PR_SUCCESS);
            if (s) {
                PR_Send(s, "nspr ping", 9, 0, PR_INTERVAL_NO_TIMEOUT);
                p.fd = s; p.in_flags = PR_POLL_READ; p.out_flags = 0;
                n = PR_Poll(&p, 1, PR_SecondsToInterval(5));
                ok("PR_Poll vede la risposta", n == 1 && (p.out_flags & PR_POLL_READ));
                n = PR_Recv(s, eco, sizeof(eco) - 1, 0, PR_SecondsToInterval(5));
                ok("PR_Recv: torna uguale", n == 9 && memcmp(eco, "nspr ping", 9) == 0);
                PR_Close(s);
            }
        }
    }

    {
        PRAddrInfo *ai = PR_GetAddrInfoByName("example.com", PR_AF_UNSPEC, PR_AI_ADDRCONFIG);
        PRNetAddr   a;
        char        testo[32] = "?";

        if (ai && PR_EnumerateAddrInfo(NULL, ai, 443, &a)) PR_NetAddrToString(&a, testo, sizeof(testo));
        printf("        (example.com con NSPR: %s)\n", ai ? testo : "non risolto");
        if (ai) PR_FreeAddrInfo(ai);
    }

    printf("prova-nspr: %d NO\n", g_no);
    return g_no;
}

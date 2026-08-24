/* =============================================================================
 * bin/polltest/polltest.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Prova di poll() e select().
 *
 *     polltest              il padre: fa le prove
 *     polltest -f <pid>     il figlio: guarda il padre e gli manda un messaggio
 *
 * ! LA PROVA CHE CONTA NON E' CHE poll() TORNI, E' CHE NEL FRATTEMPO IL
 * PROCESSO DORMA. Un'attesa attiva darebbe gli stessi identici risultati su
 * ogni altra prova di questo file: stessi revents, stessi valori di ritorno,
 * solo con la CPU bruciata. Percio' il figlio, prima di svegliare il padre,
 * legge il suo stato con procinfo() e glielo manda dentro il messaggio: il
 * padre verifica di essere stato BLOCKED mentre credeva di aspettare.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `polltest -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("polltest", "0.001");

#define STATO_BLOCKED   3       /* vedi ProcInfo.state in libc.h */
#define MSG_SVEGLIA     0x9001

static int errori = 0;

static void esito(const char *cosa, int ok)
{
    printf("  %-46s %s\n", cosa, ok ? "ok" : "FALLITA");
    if (!ok) errori++;
}

/* -----------------------------------------------------------------------------
 * Il figlio: guarda che stato ha il padre, poi lo sveglia
 * --------------------------------------------------------------------------- */
static int figlio(unsigned int pid_padre)
{
    ProcInfo     v[PROCINFO_MAX_BATCH];
    unsigned int stato = 0;
    unsigned int start = 0;
    int          n, i;

    /* Un attimo, perche' il padre deve fare in tempo a entrare in poll(). */
    usleep(300000);

    /* Cerca il padre fra i processi e prende il suo stato. */
    for (;;) {
        n = procinfo(v, PROCINFO_MAX_BATCH, start);
        if (n <= 0) break;
        for (i = 0; i < n; i++)
            if (v[i].pid == pid_padre) stato = v[i].state;
        start += (unsigned int)n;
    }

    /* Il messaggio E' la sveglia, e porta con se' la prova. */
    ipc_send(pid_padre, MSG_SVEGLIA, &stato, sizeof(stato));
    return 0;
}

/* -----------------------------------------------------------------------------
 * Il padre
 * --------------------------------------------------------------------------- */
static int padre(const char *io)
{
    struct pollfd v[3];
    int    p[2];
    int    rc;

    printf("Attesa su piu' sorgenti: poll() e select()\n\n");

    /* --- 1. una sorgente sempre pronta, senza aspettare ------------------- */
    v[0].fd = 1;                    /* stdout */
    v[0].events = POLLOUT;
    v[0].revents = 0;
    rc = poll(v, 1, 0);
    esito("stdout e' sempre scrivibile, con scadenza 0",
          rc == 1 && (v[0].revents & POLLOUT));

    /* --- 2. una pipe vuota: la scadenza scade davvero --------------------- */
    if (pipe(p) != 0) { printf("  pipe() fallita\n"); return 1; }

    {
        unsigned int t0 = uptime_ms(), dt;

        v[0].fd = p[0]; v[0].events = POLLIN; v[0].revents = 0;
        rc = poll(v, 1, 200);
        dt = uptime_ms() - t0;

        esito("pipe vuota: la scadenza rende 0", rc == 0 && v[0].revents == 0);
        /* Non si pretende un valore esatto: il PIT batte a 100 Hz e
         * l'arrotondamento e' per eccesso. Si pretende che il tempo sia
         * passato davvero, cioe' che non abbia reso subito. */
        esito("e ci ha messo almeno la scadenza", dt >= 190);
        if (dt < 190) printf("      passati %u ms invece di >=190\n", dt);
    }

    /* --- 3. la stessa pipe con dentro qualcosa ---------------------------- */
    write(p[1], "x", 1);
    v[0].fd = p[0]; v[0].events = POLLIN; v[0].revents = 0;
    rc = poll(v, 1, 0);
    esito("pipe con dati: POLLIN subito", rc == 1 && (v[0].revents & POLLIN));

    {
        char c;
        read(p[0], &c, 1);          /* si svuota */
    }

    /* --- 4. voci da saltare e voci non valide ----------------------------- */
    v[0].fd = -1;   v[0].events = POLLIN; v[0].revents = 0;
    v[1].fd = 30;   v[1].events = POLLIN; v[1].revents = 0;  /* mai aperto */
    rc = poll(v, 2, 0);
    esito("un fd negativo si salta in silenzio", v[0].revents == 0);
    esito("un fd mai aperto da' POLLNVAL",
          rc == 1 && (v[1].revents & POLLNVAL));

    /* --- 5. l'altro capo se n'e' andato ----------------------------------- */
    close(p[1]);
    v[0].fd = p[0]; v[0].events = POLLIN; v[0].revents = 0;
    rc = poll(v, 1, 0);
    esito("pipe senza piu' scrittori: POLLHUP",
          rc == 1 && (v[0].revents & POLLHUP));
    close(p[0]);

    /* --- 6. LA PROVA VERA: sveglia da un altro processo, dormendo --------- */
    {
        char  spid[16];
        char *argv[4];
        int   pid, stato_visto = 0, stato_uscita;
        IpcMessage meta;

        snprintf(spid, sizeof(spid), "%u", (unsigned int)getpid());
        argv[0] = (char *)io;
        argv[1] = "-f";
        argv[2] = spid;
        argv[3] = 0;

        if (pipe(p) != 0) { printf("  pipe() fallita\n"); return 1; }

        pid = spawn(io, argv);
        if (pid < 0) { printf("  spawn('%s') fallita (%d)\n", io, pid); return 1; }

        /* Due sorgenti insieme, e senza scadenza: si torna solo quando una
         * delle due ha qualcosa. La pipe non l'avra' mai — serve a provare
         * che la mailbox non e' l'unica cosa guardata. */
        v[0].fd = FD_IPC; v[0].events = POLLIN; v[0].revents = 0;
        v[1].fd = p[0];   v[1].events = POLLIN; v[1].revents = 0;

        rc = poll(v, 2, -1);

        esito("svegliato da un ALTRO processo, su 2 sorgenti",
              rc == 1 && (v[0].revents & POLLIN) && v[1].revents == 0);

        if (ipc_recv(&meta, &stato_visto, sizeof(stato_visto)) < 0)
            stato_visto = 0;

        esito("e mentre aspettavo ero BLOCKED, non in attesa attiva",
              stato_visto == STATO_BLOCKED);
        if (stato_visto != STATO_BLOCKED)
            printf("      il figlio mi ha visto in stato %d (BLOCKED e' %d)\n",
                   stato_visto, STATO_BLOCKED);

        waitpid(pid, &stato_uscita, 0);
        close(p[0]);
        close(p[1]);
    }

    /* --- 6b. la stessa cosa, ma svegliati da una PIPE --------------------- */
    /* ! E' IL CASO CHE PROVA LA REGISTRAZIONE. La mailbox si sveglia da sola:
     * ipc_send() cerca il destinatario per PID, qualunque cosa stia
     * aspettando. Una pipe no — bisogna essersi messi nel suo slot «chi
     * aspetta» prima di dormire, ed e' li' che poll() puo' sbagliare. Senza
     * questa prova, tutto il resto passerebbe anche con quel pezzo rotto. */
    {
        char      *argv[3];
        SpawnRedir a;
        int        pid, stato_uscita;

        if (pipe(p) != 0) { printf("  pipe() fallita\n"); return 1; }

        argv[0] = (char *)io;
        argv[1] = "-s";                 /* il figlio: aspetta e scrive su stdout */
        argv[2] = 0;

        a.fd = 1; a.flags = 0; a.percorso = 0; a.fd_padre = p[1];
        pid = spawn_ex(io, argv, environ, &a, 1);
        if (pid < 0) { printf("  spawn_ex('%s') fallita (%d)\n", io, pid); return 1; }

        close(p[1]);                    /* o non si vedra' mai la fine */

        v[0].fd = p[0]; v[0].events = POLLIN; v[0].revents = 0;
        rc = poll(v, 1, -1);
        esito("svegliato da una PIPE scritta da un altro processo",
              rc == 1 && (v[0].revents & POLLIN));

        waitpid(pid, &stato_uscita, 0);
        close(p[0]);
    }

    /* --- 7. stdin, che e' la sorgente piu' delicata di tutte -------------- */
    /* ! IL POSTO IN ATTESA DEL TTY E' UNO SOLO PER TUTTO IL SISTEMA, ed e' lo
     * stesso che usa la read della shell. Se poll() lo lasciasse sporco
     * uscendo, la shell resterebbe muta al prompt per sempre: e' il guasto
     * del luglio 2026, e questa prova esiste per accorgersene subito. La
     * verifica vera pero' non e' questa riga — e' il comando che si riesce a
     * battere DOPO che polltest e' uscito. */
    v[0].fd = 0; v[0].events = POLLIN; v[0].revents = 0;
    rc = poll(v, 1, 100);
    esito("stdin senza nessuno che digita: scade e basta",
          rc == 0 && v[0].revents == 0);

    /* --- 8. select(), che sta sopra poll() -------------------------------- */
    {
        fd_set leggere;
        struct timeval t;

        if (pipe(p) != 0) { printf("  pipe() fallita\n"); return 1; }
        write(p[1], "y", 1);

        FD_ZERO(&leggere);
        FD_SET(p[0], &leggere);
        t.tv_sec = 0; t.tv_usec = 0;

        rc = select(p[0] + 1, &leggere, 0, 0, &t);
        esito("select() vede la pipe pronta",
              rc == 1 && FD_ISSET(p[0], &leggere));

        FD_ZERO(&leggere);
        FD_SET(p[0], &leggere);
        {
            char c;
            read(p[0], &c, 1);
        }
        t.tv_sec = 0; t.tv_usec = 50000;
        rc = select(p[0] + 1, &leggere, 0, 0, &t);
        esito("select() su pipe vuota rende 0",
              rc == 0 && !FD_ISSET(p[0], &leggere));

        close(p[0]);
        close(p[1]);
    }

    printf("\n");
    if (errori == 0) printf("esito   tutto a posto\n");
    else             printf("esito   %d prove FALLITE\n", errori);

    return errori == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *io = (argv[0] != 0 && argv[0][0] == '/') ? argv[0]
                                                         : "/bin/polltest";

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 's') {
        /* Il figlio che sveglia con una pipe: il suo stdout E' l'estremita'
         * di scrittura, messa li' da spawn_ex. Aspetta, cosi' il padre fa in
         * tempo ad addormentarsi, e poi scrive un byte. */
        usleep(300000);
        write(1, "x", 1);
        return 0;
    }

    if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'f')
        return figlio((unsigned int)atoi(argv[2]));

    return padre(io);
}

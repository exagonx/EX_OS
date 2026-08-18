/* =============================================================================
 * kernel/ipc/pty.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il perche' sta in kernel/include/pty.h. Qui c'e' come.
 *
 * ! LA STRUTTURA E' QUELLA DELLA PIPE, DUE VOLTE, PIU' LA DISCIPLINA. Le regole
 * di confine sono le stesse e non si reinventano: leggere da un buffer vuoto
 * con l'altra estremita' ancora aperta vuol dire ASPETTARE; senza piu' nessuno
 * dall'altra parte vuol dire fine dei dati. Vedi kernel/include/pipe.h, dove
 * quelle tre regole sono scritte per esteso.
 * ============================================================================= */

#include "kernel.h"
#include "pty.h"
#include "sched.h"
#include "syscall.h"    /* gli errno del kernel */

/* Le quattro che ogni ambiente freestanding deve fornire; vedi
 * kernel/arch/x86/memfun.c. */
void *memset(void *dst, int c, uint32_t n);

typedef struct {
    int      usata;

    /* Verso lo SLAVE: cio' che e' stato battuto, gia' passato dalla
     * disciplina. */
    uint8_t  in[PTY_DIM];
    uint32_t in_testa, in_quanti;

    /* Verso il MASTER: cio' che il programma stampa, piu' l'eco. */
    uint8_t  out[PTY_DIM];
    uint32_t out_testa, out_quanti;

    /* La riga in costruzione, che non e' ancora di nessuno. */
    uint8_t  riga[PTY_RIGA_MAX];
    uint32_t riga_n;

    uint32_t modo;
    uint32_t pid_fg;            /* chi interrompe Ctrl+C, 0 = nessuno */
    uint32_t righe, colonne;

    uint32_t master_aperti, slave_aperti;
    uint32_t pid_att_in;        /* chi dorme aspettando di leggere lo slave  */
    uint32_t pid_att_out;       /* chi dorme aspettando di leggere il master */

    /* ! FINE DEI DATI E' UNO STATO, NON UN BYTE. Ctrl+D non si mette nel
     * buffer: se ci finisse dentro, un programma lo leggerebbe come carattere
     * 4 e la fine dei dati non arriverebbe mai. */
    uint32_t fine_dati;
} Pty;

static Pty g_pty[PTY_MAX];

static Pty *pty_da_handle(int h)
{
    if (h < 0 || h >= PTY_MAX) return NULL;
    if (!g_pty[h].usata)       return NULL;
    return &g_pty[h];
}

/* -----------------------------------------------------------------------------
 * I due anelli
 * --------------------------------------------------------------------------- */
static void metti(uint8_t *buf, uint32_t *testa, uint32_t *quanti, uint8_t c)
{
    /* ! PIENO VUOL DIRE BUTTARE L'ULTIMO ARRIVATO, e qui e' la scelta giusta:
     * l'alternativa sarebbe bloccare chi scrive, ma chi scrive nel tubo di
     * ritorno e' il KERNEL mentre fa l'eco di un tasto. Un terminale che non
     * legge abbastanza in fretta perde caratteri; se bloccasse, fermerebbe chi
     * batte. */
    if (*quanti >= PTY_DIM) return;
    buf[(*testa + *quanti) % PTY_DIM] = c;
    (*quanti)++;
}

static uint32_t prendi(uint8_t *buf, uint32_t *testa, uint32_t *quanti,
                       uint8_t *dst, uint32_t n)
{
    uint32_t presi = (n < *quanti) ? n : *quanti;
    uint32_t i;

    for (i = 0; i < presi; i++) dst[i] = buf[(*testa + i) % PTY_DIM];
    *testa   = (*testa + presi) % PTY_DIM;
    *quanti -= presi;
    return presi;
}

static void eco(Pty *p, uint8_t c)
{
    if (p->modo & PTY_ECO) metti(p->out, &p->out_testa, &p->out_quanti, c);
}

static void eco_str(Pty *p, const char *s)
{
    while (*s) eco(p, (uint8_t)*s++);
}

/* La riga finita passa allo slave. */
static void consegna_riga(Pty *p)
{
    uint32_t i;

    for (i = 0; i < p->riga_n; i++)
        metti(p->in, &p->in_testa, &p->in_quanti, p->riga[i]);
    p->riga_n = 0;
}

/* -----------------------------------------------------------------------------
 * La disciplina di linea: quello che succede a un tasto fra il master e lo
 * slave. E' l'unica parte di questo file che non e' una pipe.
 * --------------------------------------------------------------------------- */
static void disciplina(Pty *p, uint8_t c)
{
    if (!(p->modo & PTY_CANONICO)) {
        metti(p->in, &p->in_testa, &p->in_quanti, c);
        eco(p, c);
        return;
    }

    switch (c) {

    /* ! Ctrl+C NON E' UN CARATTERE DA CONSEGNARE, ED E' TUTTO IL PUNTO DI
     * QUESTO LAVORO. Finora arrivava allo slave come il byte 3, cioe' come una
     * lettera qualunque che nessuno guardava. Adesso interrompe chi sta in
     * primo piano — e se nessuno si e' dichiarato tale, non fa niente: meglio
     * un Ctrl+C che non morde di un Ctrl+C che ammazza la shell. */
    case 3:
        eco_str(p, "^C\n");
        p->riga_n = 0;
        if (p->pid_fg) proc_interrompi_locked(p->pid_fg);   /* siamo col cli */
        break;

    /* ! Ctrl+D VALE SOLO A RIGA VUOTA, come su ogni Unix. A meta' riga
     * significherebbe «finisci qui» su qualcosa che l'utente sta ancora
     * scrivendo, ed e' il modo piu' facile di perdere una riga battuta. */
    case 4:
        if (p->riga_n == 0) {
            p->fine_dati = 1;
            if (p->pid_att_in) { sched_unblock_locked(p->pid_att_in); p->pid_att_in = 0; }
        }
        break;

    case '\b':
    case 127:
        /* ! CANCELLARE E' TRE CARATTERI, NON UNO: indietro, uno spazio sopra
         * quello che c'era, indietro di nuovo. Mandare solo il Backspace
         * sposta il cursore e lascia la lettera dov'era. */
        if (p->riga_n > 0) {
            p->riga_n--;
            eco_str(p, "\b \b");
        }
        break;

    case '\r':
    case '\n':
        if (p->riga_n < PTY_RIGA_MAX) p->riga[p->riga_n++] = '\n';
        eco(p, '\n');
        consegna_riga(p);
        break;

    default:
        /* ! I TASTI SPECIALI NON ENTRANO IN UNA RIGA DI TESTO. Frecce e
         * compagnia arrivano qui come codici oltre il byte: infilarli nella
         * riga darebbe una stringa con dentro valori che non si stampano. */
        if (c >= 0x20 && c < 0x7F) {
            if (p->riga_n < PTY_RIGA_MAX - 1) {
                p->riga[p->riga_n++] = c;
                eco(p, c);
            }
        }
        break;
    }
}

/* -----------------------------------------------------------------------------
 * Aprire e chiudere
 * --------------------------------------------------------------------------- */
int32_t pty_apri(int *master, int *slave)
{
    int i;

    for (i = 0; i < PTY_MAX; i++) {
        if (g_pty[i].usata) continue;

        memset(&g_pty[i], 0, sizeof(Pty));
        g_pty[i].usata   = 1;
        g_pty[i].modo    = PTY_CANONICO | PTY_ECO;
        g_pty[i].righe   = 25;
        g_pty[i].colonne = 80;
        g_pty[i].master_aperti = 1;
        g_pty[i].slave_aperti  = 1;

        *master = i;
        *slave  = i;
        return 0;
    }
    return ERR(EMFILE);
}

void pty_apri_riferimento(int h, int master)
{
    Pty *p = pty_da_handle(h);

    if (!p) return;
    if (master) p->master_aperti++;
    else        p->slave_aperti++;
}

void pty_chiudi(int h, int master)
{
    Pty *p = pty_da_handle(h);

    if (!p) return;

    if (master) { if (p->master_aperti) p->master_aperti--; }
    else        { if (p->slave_aperti)  p->slave_aperti--;  }

    /* ! CHI RESTA DALL'ALTRA PARTE VA SVEGLIATO, non lasciato ad aspettare
     * qualcuno che non c'e' piu'. E' la regola 2 delle pipe: vuoto e senza
     * l'altro capo vuol dire fine dei dati, ma se dorme non lo scopre. */
    if (p->pid_att_in)  { sched_unblock(p->pid_att_in);  p->pid_att_in = 0; }
    if (p->pid_att_out) { sched_unblock(p->pid_att_out); p->pid_att_out = 0; }

    if (p->master_aperti == 0 && p->slave_aperti == 0) {
        memset(p, 0, sizeof(Pty));
    }
}

/* -----------------------------------------------------------------------------
 * Le quattro strade
 * --------------------------------------------------------------------------- */
int32_t pty_scrivi_master(int h, const void *buf, uint32_t n)
{
    const uint8_t *src = (const uint8_t *)buf;
    Pty     *p;
    uint32_t i;

    interrupts_disable();
    p = pty_da_handle(h);
    if (!p) { interrupts_enable(); return ERR(EBADF); }

    for (i = 0; i < n; i++) disciplina(p, src[i]);

    /* Chi aspettava una riga puo' averla adesso; e l'eco ha messo roba anche
     * nel tubo di ritorno. */
    if (p->in_quanti && p->pid_att_in) {
        sched_unblock_locked(p->pid_att_in);
        p->pid_att_in = 0;
    }
    if (p->out_quanti && p->pid_att_out) {
        sched_unblock_locked(p->pid_att_out);
        p->pid_att_out = 0;
    }

    interrupts_enable();
    return (int32_t)n;
}

int32_t pty_scrivi_slave(int h, const void *buf, uint32_t n)
{
    const uint8_t *src = (const uint8_t *)buf;
    Pty     *p;
    uint32_t i;

    interrupts_disable();
    p = pty_da_handle(h);
    if (!p) { interrupts_enable(); return ERR(EBADF); }

    if (p->master_aperti == 0) { interrupts_enable(); return ERR(EPIPE); }

    for (i = 0; i < n; i++)
        metti(p->out, &p->out_testa, &p->out_quanti, src[i]);

    if (p->pid_att_out) {
        sched_unblock_locked(p->pid_att_out);
        p->pid_att_out = 0;
    }

    interrupts_enable();
    return (int32_t)n;
}

int32_t pty_leggi_slave(int h, void *buf, uint32_t n)
{
    uint8_t  appoggio[PTY_DIM];
    uint8_t *dst = (uint8_t *)buf;
    Pty     *p;
    uint32_t presi, i;

    if (n == 0) return 0;
    if (n > PTY_DIM) n = PTY_DIM;

    for (;;) {
        Process *cur;

        interrupts_disable();
        p = pty_da_handle(h);
        if (!p) { interrupts_enable(); return ERR(EBADF); }

        if (p->in_quanti > 0) break;

        /* Fine dei dati: Ctrl+D, o il master se n'e' andato. */
        if (p->fine_dati || p->master_aperti == 0) {
            p->fine_dati = 0;
            interrupts_enable();
            return 0;
        }

        cur = proc_get_current();
        if (!cur) { interrupts_enable(); return ERR(EAGAIN); }

        p->pid_att_in = cur->pid;
        sched_block(PROC_BLOCKED);

        /* Vedi pipe_leggi: chi e' stato interrotto non torna ad aspettare. */
        if (proc_interrotto()) return ERR(EINTR);
    }

    presi = prendi(p->in, &p->in_testa, &p->in_quanti, appoggio, n);
    interrupts_enable();

    for (i = 0; i < presi; i++) dst[i] = appoggio[i];
    return (int32_t)presi;
}

int32_t pty_leggi_master(int h, void *buf, uint32_t n)
{
    uint8_t  appoggio[PTY_DIM];
    uint8_t *dst = (uint8_t *)buf;
    Pty     *p;
    uint32_t presi, i;

    if (n == 0) return 0;
    if (n > PTY_DIM) n = PTY_DIM;

    for (;;) {
        Process *cur;

        interrupts_disable();
        p = pty_da_handle(h);
        if (!p) { interrupts_enable(); return ERR(EBADF); }

        if (p->out_quanti > 0) break;

        if (p->slave_aperti == 0) { interrupts_enable(); return 0; }

        cur = proc_get_current();
        if (!cur) { interrupts_enable(); return ERR(EAGAIN); }

        p->pid_att_out = cur->pid;
        sched_block(PROC_BLOCKED);

        if (proc_interrotto()) return ERR(EINTR);
    }

    presi = prendi(p->out, &p->out_testa, &p->out_quanti, appoggio, n);
    interrupts_enable();

    for (i = 0; i < presi; i++) dst[i] = appoggio[i];
    return (int32_t)presi;
}

/* -----------------------------------------------------------------------------
 * Controllo e stato
 * --------------------------------------------------------------------------- */
int32_t pty_ctl(int h, uint32_t cmd, uint32_t arg)
{
    Pty *p = pty_da_handle(h);

    if (!p) return ERR(EBADF);

    switch (cmd) {
    case PTY_CTL_FG:
        p->pid_fg = arg;
        return 0;

    case PTY_CTL_MODO:
        p->modo = arg & (PTY_CANONICO | PTY_ECO);
        /* ! PASSANDO A GREZZO SI BUTTA LA RIGA A META'. Tenerla vorrebbe dire
         * consegnarla piu' tardi in un modo in cui nessuno la sta aspettando:
         * chi cambia modo lo fa perche' vuole i tasti da adesso. */
        p->riga_n = 0;
        return 0;

    case PTY_CTL_MISURA:
        p->righe   = (arg >> 16) & 0xFFFF;
        p->colonne = arg & 0xFFFF;
        return 0;

    case PTY_CTL_LEGGI_MISURA:
        return (int32_t)((p->righe << 16) | (p->colonne & 0xFFFF));

    default:
        return ERR(EINVAL);
    }
}

/* -----------------------------------------------------------------------------
 * L'attesa di poll()
 *
 * ! SI USA LO STESSO CAMPO DI CHI DORME IN read(), e il limite che ne segue va
 * detto: un pty ha UN attendente per direzione. Nel suo uso — un master da una
 * parte, una shell dall'altra — non ce ne sono due; se un giorno due processi
 * aspettassero la stessa estremita', il secondo scalzerebbe il primo e quello
 * resterebbe a dormire finche' non arriva altro. Le pipe hanno una lista
 * proprio per questo, e il giorno che serve si copia da li'.
 * --------------------------------------------------------------------------- */
int pty_attesa_registra_locked(int h, int master, uint32_t pid)
{
    Pty *p = pty_da_handle(h);

    if (!p) return 0;
    if (master) p->pid_att_out = pid;
    else        p->pid_att_in  = pid;
    return 1;
}

void pty_attesa_togli_locked(int h, int master, uint32_t pid)
{
    Pty *p = pty_da_handle(h);

    if (!p) return;
    if (master) { if (p->pid_att_out == pid) p->pid_att_out = 0; }
    else        { if (p->pid_att_in  == pid) p->pid_att_in  = 0; }
}

int pty_pronto_slave(int h)
{
    Pty *p = pty_da_handle(h);

    if (!p) return 1;                       /* sparito: pronto a fallire */
    return (p->in_quanti > 0 || p->fine_dati || p->master_aperti == 0);
}

int pty_pronto_master(int h)
{
    Pty *p = pty_da_handle(h);

    if (!p) return 1;
    return (p->out_quanti > 0 || p->slave_aperti == 0);
}

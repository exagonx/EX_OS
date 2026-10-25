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

    PtyTermios t;               /* the modes: see pty.h */
    uint32_t pid_fg;            /* chi interrompe Ctrl+C, 0 = nessuno */
    uint32_t righe, colonne;

    uint32_t master_aperti, slave_aperti;
    uint32_t pid_att_in;        /* chi dorme aspettando di leggere lo slave  */
    uint32_t pid_att_out;       /* chi dorme aspettando di leggere il master */
    uint32_t pid_att_spazio;    /* chi dorme aspettando POSTO nel tubo verso
                                   il master: vedi pty_scrivi_slave          */

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
    /* ! PIENO VUOL DIRE BUTTARE L'ULTIMO ARRIVATO, ED E' LA SCELTA GIUSTA PER
     * L'ECO E BASTA. Chi scrive qui dentro facendo l'eco e' il KERNEL mentre
     * un tasto passa: se bloccasse, fermerebbe chi batte. Un terminale che non
     * legge abbastanza in fretta perde l'eco di un carattere, e pazienza.
     *
     * ! MA PER L'USCITA DI UN PROGRAMMA NON VALE, e per mesi si e' applicata
     * anche li'. pty_scrivi_slave riempiva il tubo con questa, che a tubo
     * pieno non scrive e non lo dice, e poi rendeva n — cioe' «scritto tutto».
     * Il risultato: un programma che stampa piu' di PTY_DIM byte piu' in
     * fretta di quanto il master legga ne perde il resto, IN SILENZIO, e la
     * write gli ha appena detto che e' andato tutto bene.
     *
     * ! E DA TELNET SEMBRAVA TUTT'ALTRO. Un referto di 1405 byte ne faceva
     * arrivare mille e poi piu' niente; il programma pero' arrivava in fondo e
     * la shell tornava al prompt, quindi non sembrava una perdita di dati —
     * sembrava un programma che moriva, ogni volta a un'istruzione diversa.
     * Mezza giornata di diagnosi su un driver che non aveva niente che non
     * andasse. Il numero che lo dice: PTY_DIM e' 1024.
     *
     * Adesso pty_scrivi_slave non passa piu' di qui quando il tubo e' pieno:
     * si ferma e aspetta posto. Vedi la' sotto. */
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

/* ! THE ECHO GOES THROUGH THE OUTPUT PROCESSING TOO (ONLCR), as on every
 * Unix: until termios the newline became CR LF in telnetd, which could not
 * know whether a program had switched that off. */
static void eco(Pty *p, uint8_t c)
{
    if (!(p->t.c_lflag & T_ECHO)) return;
    if (c == '\n' && (p->t.c_oflag & (T_OPOST | T_ONLCR)) == (T_OPOST | T_ONLCR))
        metti(p->out, &p->out_testa, &p->out_quanti, '\r');
    metti(p->out, &p->out_testa, &p->out_quanti, c);
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
    const uint8_t *cc = p->t.c_cc;

    /* --- input processing: what the termios says a byte becomes --- */
    if (p->t.c_iflag & T_ISTRIP) c &= 0x7F;
    if (c == '\r') {
        if (p->t.c_iflag & T_IGNCR) return;
        if (p->t.c_iflag & T_ICRNL) c = '\n';
    } else if (c == '\n' && (p->t.c_iflag & T_INLCR)) {
        c = '\r';
    }

    /* ! Ctrl+C NON E' UN CARATTERE DA CONSEGNARE, ED E' TUTTO IL PUNTO DI
     * QUESTO LAVORO. Finora arrivava allo slave come il byte 3, cioe' come una
     * lettera qualunque che nessuno guardava. Adesso interrompe chi sta in
     * primo piano — e se nessuno si e' dichiarato tale, non fa niente: meglio
     * un Ctrl+C che non morde di un Ctrl+C che ammazza la shell.
     * ! WITH ISIG ONLY: a program that switched it off (an editor, ncurses in
     * raw mode) wants the 3 as a key. */
    if ((p->t.c_lflag & T_ISIG) && cc[T_VINTR] && c == cc[T_VINTR]) {
        eco_str(p, "^C\n");
        if (!(p->t.c_lflag & T_NOFLSH)) { p->riga_n = 0; p->in_quanti = 0; }
        if (p->pid_fg) proc_interrompi_locked(p->pid_fg);   /* siamo col cli */
        return;
    }

    if (!(p->t.c_lflag & T_ICANON)) {
        metti(p->in, &p->in_testa, &p->in_quanti, c);
        eco(p, c);
        return;
    }

    /* ! Ctrl+D VALE SOLO A RIGA VUOTA, come su ogni Unix. A meta' riga
     * significherebbe «finisci qui» su qualcosa che l'utente sta ancora
     * scrivendo, ed e' il modo piu' facile di perdere una riga battuta. */
    if (cc[T_VEOF] && c == cc[T_VEOF]) {
        if (p->riga_n == 0) {
            p->fine_dati = 1;
            if (p->pid_att_in) { sched_unblock_locked(p->pid_att_in); p->pid_att_in = 0; }
        }
        return;
    }

    /* ! CANCELLARE E' TRE CARATTERI, NON UNO: indietro, uno spazio sopra
     * quello che c'era, indietro di nuovo. Mandare solo il Backspace
     * sposta il cursore e lascia la lettera dov'era. Both DEL (the VERASE
     * of an xterm) and Backspace erase, whatever VERASE says: a terminal
     * that sends the other one must not leave the user without a way to
     * correct. */
    if (c == '\b' || c == 127 || (cc[T_VERASE] && c == cc[T_VERASE])) {
        if (p->riga_n > 0) {
            p->riga_n--;
            if (p->t.c_lflag & T_ECHOE) eco_str(p, "\b \b");
        }
        return;
    }

    /* Ctrl+U: the whole line. */
    if (cc[T_VKILL] && c == cc[T_VKILL]) {
        while (p->riga_n > 0) {
            p->riga_n--;
            if (p->t.c_lflag & T_ECHOK) eco_str(p, "\b \b");
        }
        return;
    }

    if (c == '\n') {
        if (p->riga_n < PTY_RIGA_MAX) p->riga[p->riga_n++] = '\n';
        eco(p, '\n');
        consegna_riga(p);
        return;
    }

    /* ! I TASTI SPECIALI NON ENTRANO IN UNA RIGA DI TESTO: the control
     * characters stay out. Bytes from 0x80 go in: they are UTF-8. */
    if (c >= 0x20 && c != 0x7F) {
        if (p->riga_n < PTY_RIGA_MAX - 1) {
            p->riga[p->riga_n++] = c;
            eco(p, c);
        }
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
        /* The modes of a fresh terminal on any Unix ("stty sane"). */
        g_pty[i].t.c_iflag = T_ICRNL;
        g_pty[i].t.c_oflag = T_OPOST | T_ONLCR;
        g_pty[i].t.c_cflag = 0x00BF;        /* B38400 | CS8 | CREAD */
        g_pty[i].t.c_lflag = T_ISIG | T_ICANON | T_ECHO | T_ECHOE | T_ECHOK;
        g_pty[i].t.c_cc[T_VINTR]  = 3;
        g_pty[i].t.c_cc[1]        = 28;     /* VQUIT */
        g_pty[i].t.c_cc[T_VERASE] = 127;
        g_pty[i].t.c_cc[T_VKILL]  = 21;
        g_pty[i].t.c_cc[T_VEOF]   = 4;
        g_pty[i].t.c_cc[T_VMIN]   = 1;
        g_pty[i].t.c_cc[10]       = 26;     /* VSUSP */
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

/* ! DUE VERSIONI, COME PER LE PIPE, E PER LA STESSA IDENTICA RAGIONE. Chi
 * chiude un descrittore dal syscall gira con gli interrupt accesi e puo'
 * chiamare sched_unblock(); chi li chiude perche' un processo sta MORENDO gira
 * gia' in sezione critica — proc_chiudi_fd() presuppone `cli` — e li' serve la
 * variante _locked. Chiamare quella sbagliata riaccende gli interrupt in mezzo
 * allo smontaggio di un processo.
 *
 * Vedi il commento sopra proc_chiudi_fd in kernel/sched/sched.c e le
 * pipe_chiudi_*_locked in kernel/include/pipe.h. */
static void pty_chiudi_comune(int h, int master, int in_sezione_critica)
{
    Pty *p = pty_da_handle(h);

    if (!p) return;

    if (master) { if (p->master_aperti) p->master_aperti--; }
    else        { if (p->slave_aperti)  p->slave_aperti--;  }

    if (p->pid_att_in) {
        if (in_sezione_critica) sched_unblock_locked(p->pid_att_in);
        else                    sched_unblock(p->pid_att_in);
        p->pid_att_in = 0;
    }
    if (p->pid_att_out) {
        if (in_sezione_critica) sched_unblock_locked(p->pid_att_out);
        else                    sched_unblock(p->pid_att_out);
        p->pid_att_out = 0;
    }
    /* ! E CHI ASPETTA POSTO, che senza l'altro capo non arrivera' mai. */
    if (p->pid_att_spazio) {
        if (in_sezione_critica) sched_unblock_locked(p->pid_att_spazio);
        else                    sched_unblock(p->pid_att_spazio);
        p->pid_att_spazio = 0;
    }

    if (p->master_aperti == 0 && p->slave_aperti == 0)
        memset(p, 0, sizeof(Pty));
}

void pty_chiudi_locked(int h, int master)
{
    pty_chiudi_comune(h, master, 1);
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
    if (p->pid_att_spazio) { sched_unblock(p->pid_att_spazio);
                             p->pid_att_spazio = 0; }

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

/* =============================================================================
 * pty_scrivi_slave — l'uscita di un programma, e perche' NON puo' perdersi
 *
 * ! LA REGOLA DI UN TERMINALE E' CHE CHI SCRIVE ASPETTA. Un programma che
 * stampa piu' in fretta di quanto il terminale legga non perde l'uscita: si
 * ferma. E' cosi' su ogni Unix, ed e' l'unico comportamento che permette a un
 * programma di credere alla propria write.
 *
 * ! QUEL CHE C'ERA PRIMA riempiva il tubo con metti(), che a tubo pieno non
 * scrive e non lo dice, e poi rendeva n. Il perche' del danno sta nel commento
 * di metti(): in breve, oltre 1024 byte l'uscita spariva in silenzio.
 *
 * ! SI RENDE QUEL CHE SI E' SCRITTO, e l'attesa e' INTERROMPIBILE. Un Ctrl+C
 * mentre un programma stampa non deve lasciarlo dentro il kernel: si torna con
 * il conto parziale, che e' esattamente cio' che POSIX prescrive per una write
 * interrotta dopo aver gia' trasferito qualcosa.
 *
 * ! E SE IL MASTER SE NE VA MENTRE ASPETTIAMO, si torna con quel che si e'
 * scritto — o EPIPE se non si e' scritto niente. Restare a dormire aspettando
 * posto in un tubo che nessuno svuotera' piu' e' il modo di perdere un
 * processo per sempre.
 *
 * ! IL POSTO PER CHI ASPETTA E' UNO SOLO, come per le altre due attese di
 * questo file. Se un secondo processo scrive sullo stesso slave mentre il
 * primo dorme, NON gli si porta via il posto: si rende il conto parziale e
 * tocca a lui riprovare. Sovrascrivere quel campo vorrebbe dire un processo
 * che non si sveglia piu', ed e' un difetto che non si vede dove e' stato
 * fatto.
 * ========================================================================== */
int32_t pty_scrivi_slave(int h, const void *buf, uint32_t n)
{
    const uint8_t *src = (const uint8_t *)buf;
    Pty     *p;
    uint32_t scritti = 0;

    if (n == 0) return 0;

    for (;;) {
        Process *cur;

        interrupts_disable();
        p = pty_da_handle(h);
        if (!p) { interrupts_enable(); return ERR(EBADF); }

        if (p->master_aperti == 0) {
            interrupts_enable();
            return scritti ? (int32_t)scritti : ERR(EPIPE);
        }

        /* ! OPOST+ONLCR: the newline becomes CR LF here, and only when the
         * modes say so. A full-screen program switches it off and moves the
         * cursor down with a bare LF; when telnetd did it, it could not. */
        while (scritti < n && p->out_quanti < PTY_DIM) {
            if (src[scritti] == '\n' &&
                (p->t.c_oflag & (T_OPOST | T_ONLCR)) == (T_OPOST | T_ONLCR)) {
                if (p->out_quanti > PTY_DIM - 2) break;
                metti(p->out, &p->out_testa, &p->out_quanti, '\r');
            }
            metti(p->out, &p->out_testa, &p->out_quanti, src[scritti++]);
        }

        /* ! SI SVEGLIA IL LETTORE PRIMA DI ANDARE A DORMIRE, sempre: e' lui
         * che fara' il posto che stiamo per aspettare. */
        if (p->pid_att_out) {
            sched_unblock_locked(p->pid_att_out);
            p->pid_att_out = 0;
        }

        if (scritti == n) { interrupts_enable(); return (int32_t)scritti; }

        cur = proc_get_current();
        if (!cur) { interrupts_enable(); return (int32_t)scritti; }

        if (p->pid_att_spazio && p->pid_att_spazio != cur->pid) {
            interrupts_enable();
            return (int32_t)scritti;      /* c'e' gia' qualcuno in attesa */
        }

        p->pid_att_spazio = cur->pid;
        sched_block(PROC_BLOCKED);

        if (proc_interrotto()) return (int32_t)scritti;
    }
}

/* =============================================================================
 * pty_leggi_slave — and, without ICANON, VMIN and VTIME
 *
 *   VMIN>0 VTIME=0   wait until VMIN bytes (at most n) are there
 *   VMIN=0 VTIME=0   what is there, now; 0 if nothing
 *   VMIN=0 VTIME>0   wait for one byte at most VTIME tenths of a second
 *   VMIN>0 VTIME>0   the first byte without limit, then VTIME between bytes
 *
 * ! THE TIMED WAITS LOOK EVERY 10 MS instead of arming a timer: the pty has
 * one waiter field per direction, and a sleeping reader that a writer also
 * wakes would need both. Ten milliseconds is one tick; an ESC alone and the
 * start of an arrow key are told apart by tens of them (ncurses: ESCDELAY).
 * ========================================================================== */
int32_t pty_leggi_slave(int h, void *buf, uint32_t n)
{
    uint8_t  appoggio[PTY_DIM];
    uint8_t *dst = (uint8_t *)buf;
    Pty     *p;
    uint32_t presi, i, serve = 1, decimi = 0, inizio = 0, visti = 0;
    int      partito = 0;           /* the VMIN=0 timer is running */
    int      canonico;

    if (n == 0) return 0;
    if (n > PTY_DIM) n = PTY_DIM;

    interrupts_disable();
    p = pty_da_handle(h);
    if (!p) { interrupts_enable(); return ERR(EBADF); }
    canonico = (p->t.c_lflag & T_ICANON) != 0;
    if (!canonico) {
        serve  = p->t.c_cc[T_VMIN];
        decimi = p->t.c_cc[T_VTIME];
        if (serve > n) serve = n;
    }
    interrupts_enable();

    for (;;) {
        Process *cur;
        int      a_tempo = 0;       /* 1: look again in a tick */

        interrupts_disable();
        p = pty_da_handle(h);
        if (!p) { interrupts_enable(); return ERR(EBADF); }

        if (canonico || (serve > 0 && decimi == 0)) {
            if (p->in_quanti >= (canonico ? 1u : serve)) break;
        } else if (serve == 0 && decimi == 0) {
            break;                                      /* what is there */
        } else if (serve == 0) {                        /* one byte, timed */
            if (p->in_quanti > 0) break;
            if (!partito) { inizio = g_ticks; partito = 1; }
            if (g_ticks - inizio >= decimi * 10u) break;
            a_tempo = 1;
        } else {                                        /* VTIME between bytes */
            if (p->in_quanti >= serve) break;
            if (p->in_quanti > 0) {
                if (p->in_quanti != visti) { visti = p->in_quanti; inizio = g_ticks; }
                if (g_ticks - inizio >= decimi * 10u) break;
                a_tempo = 1;
            }
        }

        /* Fine dei dati: Ctrl+D, o il master se n'e' andato. */
        if (p->in_quanti == 0 && (p->fine_dati || p->master_aperti == 0)) {
            p->fine_dati = 0;
            interrupts_enable();
            return 0;
        }
        if (p->in_quanti > 0 && p->master_aperti == 0) break;

        cur = proc_get_current();
        if (!cur) { interrupts_enable(); return ERR(EAGAIN); }

        if (a_tempo) {
            interrupts_enable();
            sched_sleep(10);
        } else {
            p->pid_att_in = cur->pid;
            sched_block(PROC_BLOCKED);
        }

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

    /* ! ADESSO C'E' POSTO, E CHI ASPETTAVA VA SVEGLIATO. Senza questa riga
     * pty_scrivi_slave dormirebbe per sempre: il posto si libera solo qui. */
    if (p->pid_att_spazio) {
        sched_unblock_locked(p->pid_att_spazio);
        p->pid_att_spazio = 0;
    }

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
        /* ! THE OLD RAW MODE LET Ctrl+C THROUGH AS A BYTE, and its users
         * (extaskbar, exuser's password prompt) count on it: without
         * PTY_CANONICO, ISIG goes too. tcsetattr is the way to choose. */
        p->t.c_lflag &= ~(uint32_t)(T_ICANON | T_ECHO | T_ISIG);
        if (arg & PTY_CANONICO) p->t.c_lflag |= T_ICANON | T_ISIG;
        if (arg & PTY_ECO)      p->t.c_lflag |= T_ECHO;
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
 * pty_ioctl — the termios and the window size, from either end
 * --------------------------------------------------------------------------- */
int32_t pty_ioctl(int h, uint32_t req, uintptr_t arg)
{
    Pty *p;
    int32_t r = 0;

    interrupts_disable();
    p = pty_da_handle(h);
    if (!p) { interrupts_enable(); return ERR(EBADF); }

    switch (req) {
    case PTY_TCGETS:
        *(PtyTermios *)arg = p->t;
        break;

    case PTY_TCSETSF:                       /* flush the input, then set */
        p->in_quanti = 0; p->riga_n = 0; p->fine_dati = 0;
        /* fall through */
    case PTY_TCSETS:
    case PTY_TCSETSW: {                     /* the output is never held */
        uint32_t era = p->t.c_lflag & T_ICANON;
        p->t = *(const PtyTermios *)arg;
        /* ! LEAVING CANONICAL MODE, THE HALF LINE IS GIVEN, not thrown away
         * as PTY_CTL_MODO does: POSIX keeps it, and a readline that switches
         * mode between two keys would otherwise lose what was typed ahead. */
        if (era && !(p->t.c_lflag & T_ICANON)) consegna_riga(p);
        if (p->in_quanti && p->pid_att_in) {
            sched_unblock_locked(p->pid_att_in);
            p->pid_att_in = 0;
        }
        break;
    }

    case PTY_TCFLSH:                        /* 0 input, 1 output, 2 both */
        if (arg == 0 || arg == 2) { p->in_quanti = 0; p->riga_n = 0; }
        if (arg == 1 || arg == 2) p->out_quanti = 0;
        break;

    case PTY_TIOCGWINSZ: {
        uint16_t *w = (uint16_t *)arg;
        w[0] = (uint16_t)p->righe; w[1] = (uint16_t)p->colonne;
        w[2] = (uint16_t)(p->colonne * 8); w[3] = (uint16_t)(p->righe * 16);
        break;
    }

    case PTY_TIOCSWINSZ: {
        const uint16_t *w = (const uint16_t *)arg;
        if (w[0]) p->righe = w[0];
        if (w[1]) p->colonne = w[1];
        break;
    }

    default:
        r = ERR(ENOTTY);
    }
    interrupts_enable();
    return r;
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
    /* ! WITHOUT ICANON AND WITHOUT VTIME, READY MEANS VMIN BYTES, as Linux
     * says: a read would not return before, and poll() must not promise
     * what read() will not keep. With VTIME the read returns less after the
     * timer, so one byte is ready. */
    if (!(p->t.c_lflag & T_ICANON) && p->t.c_cc[T_VMIN] > 1 && !p->t.c_cc[T_VTIME] &&
        p->in_quanti > 0 && p->in_quanti < p->t.c_cc[T_VMIN] &&
        !p->fine_dati && p->master_aperti)
        return 0;
    return (p->in_quanti > 0 || p->fine_dati || p->master_aperti == 0);
}

int pty_pronto_master(int h)
{
    Pty *p = pty_da_handle(h);

    if (!p) return 1;
    return (p->out_quanti > 0 || p->slave_aperti == 0);
}

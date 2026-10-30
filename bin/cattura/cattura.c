/* =============================================================================
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     cattura [-f] [FILE]
 *
 * Saves what ExWin has on screen to a PNG (voce 44, 30 October 2026). -f takes
 * only the window with the focus. Without FILE the picture goes to
 * $HOME/Images/schermata-AAAAMMGG-HHMMSS.png, and the name is printed. The
 * Stamp key starts this program (pm), Alt+Stamp with -f; from telnet it is
 * the way to see the desktop of a machine without being in front of it.
 *
 * The protocol is in win_proto.h, at WIN_MSG_FOTO. The server only copies the
 * pixels: compressing is done here.
 * ============================================================================= */
#include "libc.h"
#include "win_proto.h"
#include "scrivi.h"

EX_VERSIONE("cattura", "0.001");

static int chiedi(int server, unsigned int passo, unsigned int flag, WinFotata *r)
{
    WinFoto    q;
    IpcMessage meta;
    char       buf[64];
    int        giri;

    q.passo = passo;
    q.flag  = flag;
    if (ipc_send(server, WIN_MSG_FOTO, &q, sizeof(q)) < 0) return -1;

    /* ! NOT A SINGLE LONG WAIT: 1 second at a time, for 10 at most */
    for (giri = 0; giri < 10; giri++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 1000) < 0) continue;
        if (meta.tipo == WIN_MSG_FOTATA && meta.len >= sizeof(*r)) {
            memcpy(r, buf, sizeof(*r));
            return 0;
        }
    }
    return -1;
}

static int su_fd(void *chi, const unsigned char *p, unsigned int n)
{
    int fd = *(int *)chi;

    while (n > 0) {
        int s = write(fd, p, n);

        if (s <= 0) return 0;
        p += s;
        n -= (unsigned int)s;
    }
    return 1;
}

/* $HOME/Images/schermata-...png, with -2, -3... if that second is taken */
static int nome_predefinito(char *out, unsigned int max)
{
    const char *home = getenv("HOME");
    char        dir[256];
    struct tm   t;
    struct stat st;
    time_t      ora = time(0);
    int         n;

    if (!home || !home[0]) home = "/root";
    snprintf(dir, sizeof(dir), "%s/Images", home);
    (void)mkdir(dir, 0755);
    localtime_r(&ora, &t);
    for (n = 1; n < 1000; n++) {
        int l = snprintf(out, max, "%s/schermata-%04d%02d%02d-%02d%02d%02d", dir,
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec);

        if (n > 1) l += snprintf(out + l, max - (unsigned int)l, "-%d", n);
        snprintf(out + l, max - (unsigned int)l, ".png");
        if (stat(out, &st) != 0) return 0;
    }
    return -1;
}

int main(int argc, char **argv)
{
    char         servizio[32], nome[16], file[320];
    const char  *dove = 0;
    unsigned int flag = 0;
    int          i, server, fd, ok;
    WinFotata    r;
    ShmZona      z;
    EximgBitmap  bm;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) flag |= WIN_FOTO_FUOCO;
        else if (argv[i][0] == '-') {
            printf("uso: cattura [-f] [file.png]\n");
            return 2;
        } else dove = argv[i];
    }

    win_nome_servizio(servizio, sizeof(servizio));
    server = ipc_lookup(servizio);
    if (server < 0) {
        printf("cattura: la grafica non e' attiva (si fotografa solo ExWin)\n");
        return 1;
    }
    if (chiedi(server, 0, 0, &r) != 0 || r.esito != 0 || r.w == 0 || r.h == 0) {
        printf("cattura: il server grafico non risponde\n");
        return 1;
    }

    win_nome_foto(nome, sizeof(nome), (unsigned int)getpid());
    memset(&z, 0, sizeof(z));
    memcpy(z.nome, nome, sizeof(z.nome));
    z.byte = r.w * r.h * 4u;
    z.flag = SHM_CREA;
    if (shm_apri(&z) != 0) {
        printf("cattura: niente memoria per %ux%u\n", r.w, r.h);
        return 1;
    }

    if (chiedi(server, 1, flag, &r) != 0 || r.esito != 0) {
        printf("cattura: %s\n", (flag & WIN_FOTO_FUOCO) ?
               "nessuna finestra ha il fuoco" : "il server non ha dato la schermata");
        return 1;
    }
    if (r.nascosta)
        printf("cattura: c'e' una console di testo a video, "
               "questa e' la scrivania di quando e' stata lasciata\n");

    if (!dove) {
        if (nome_predefinito(file, sizeof(file)) != 0) {
            printf("cattura: nessun nome libero\n");
            return 1;
        }
        dove = file;
    }
    fd = open(dove, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("cattura: non posso creare %s\n", dove);
        return 1;
    }

    bm.larghezza = r.w;
    bm.altezza   = r.h;
    bm.px        = (unsigned int *)(unsigned long)z.virt;
    ok = eximg_scrivi_png(&bm, su_fd, &fd);
    if (close(fd) != 0) ok = 0;
    (void)shm_chiudi(&z);
    if (!ok) {
        printf("cattura: scrittura di %s fallita\n", dove);
        (void)unlink(dove);
        return 1;
    }
    printf("%s (%ux%u)\n", dove, r.w, r.h);
    return 0;
}

/* =============================================================================
 * tools/prove/consoleprova.c - who may talk to the devices of the machine
 * (voce 111, step 4b, kernel 0.359)
 *
 *     consoleprova          as root, from a console (not over telnet)
 *
 * The user's choice of 23/11/2026: keyboard, mouse, audio and screen are of
 * root and of who logged in at a console; wserver takes only its own user and
 * root; ports below 1024 are root's. Children go down to uid 1001 or 1002
 * with setuid and try:
 *   - a mailbox with IPC_FILTRO_CONSOLE (this program's): refused while
 *     nobody sits at the console, open to 1001 once root says 1001 sits
 *     there (console_utente, what login does), closed to 1002, closed again
 *     when 1001 leaves; only root may say who sits there;
 *   - a mailbox with IPC_FILTRO_MIO, of root: closed to 1001, open to root;
 *   - kbd and fb_map (the framebuffer) with nobody at the console: refused;
 *   - bind of TCP 80 and UDP 53: EACCES; listen on 81 asked to ip.drv
 *     directly, past the libc: -EACCES; TCP 8080: allowed. Root opens UDP
 *     1023.
 *
 * Exit: how many failed.
 * ============================================================================= */
#include "libc.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define IP_MSG_TCP_ASCOLTA 14
#define IP_MSG_ESITO      128

static int g_prove, g_fallite;

static void esito(const char *cosa, int buono, const char *visto)
{
    g_prove++;
    if (!buono) g_fallite++;
    printf("  [%s] %s: %s\n", buono ? "OK" : "NO", cosa, visto);
}

/* Runs fn in a child as uid (0 = stays root); returns its exit code. */
static int come(unsigned int uid, int (*fn)(int), int arg)
{
    int pid = fork(), st = 0;

    if (pid == 0) {
        if (uid != 0) {
            setgid(uid);
            if (setuid(uid) != 0) _exit(99);
        }
        _exit(fn(arg));
    }
    if (pid < 0) return 98;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 97;
}

/* child: ipc_send to pid arg; 0 sent, 1 EPERM, 2 other */
static int manda(int pid)
{
    int r = ipc_send((unsigned)pid, 1, "x", 1);

    return r == 0 ? 0 : (r == -EPERM ? 1 : 2);
}

/* child: console_utente(1, 5) as a user: 1 EPERM */
static int dice_console(int x)
{
    (void)x;
    return console_utente(1, 5) == -EPERM ? 1 : 0;
}

/* child: fb_map: 1 EPERM, 0 mapped, 2 other */
static int mappa_fb(int x)
{
    (void)x;
    if (fb_map() != 0) return 0;
    return errno == EPERM ? 1 : 2;
}

/* (kernel 0.367, debug/bug_PRIV001) children: 0 allowed, 1 EPERM, 2 other */
static char g_klog[4096];
static int leggi_klog(int x)
{
    int r = klog_leggi(g_klog, sizeof(g_klog), 1);

    (void)x;
    return r >= 0 ? 0 : (r == -EPERM ? 1 : 2);
}
static int testo(int x)
{
    int r = modo_testo();

    (void)x;
    return r == 0 ? 0 : (r == -EPERM ? 1 : 2);
}
/* reboot with a command that does not exist: EPERM before looking at it if
 * not allowed, EINVAL if allowed - nothing goes down either way */
static int spegne(int x)
{
    int r = reboot(99);

    (void)x;
    return r == -EINVAL ? 0 : (r == -EPERM ? 1 : 2);
}

/* child: bind port arg (negative = UDP): 0 ok, 1 EACCES, 2 other */
static int lega(int porta)
{
    struct sockaddr_in a;
    int s = socket(AF_INET, porta < 0 ? SOCK_DGRAM : SOCK_STREAM, 0), r;

    if (s < 0) return 3;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons((unsigned short)(porta < 0 ? -porta : porta));
    r = bind(s, (struct sockaddr *)&a, sizeof(a));
    if (r == 0 && porta > 0) r = listen(s, 4);
    close(s);
    return r == 0 ? 0 : (errno == EACCES ? 1 : 2);
}

/* child: IP_MSG_TCP_ASCOLTA port arg straight to ip.drv: 1 -EACCES */
static int ascolta_diretto(int porta)
{
    int          ip = ipc_lookup("ip");
    unsigned int p = (unsigned)porta, inizio = uptime_ms();
    unsigned char buf[64];

    if (ip <= 0) return 3;
    if (ipc_send((unsigned)ip, IP_MSG_TCP_ASCOLTA, &p, sizeof(p)) < 0) return 4;
    while (uptime_ms() - inizio < 3000) {
        IpcMessage m;
        int        c = 0;

        if (ipc_recv_timeout(&m, buf, sizeof(buf), 3000) < 0) break;
        if ((int)m.sender_pid != ip || m.tipo != IP_MSG_ESITO) continue;
        memcpy(&c, buf, sizeof(c));
        return c == -EACCES ? 1 : 2;
    }
    return 5;
}

/* a message from pid within ms? */
static int arrivato(int pid, unsigned int ms)
{
    unsigned int inizio = uptime_ms();
    unsigned char buf[16];

    while (uptime_ms() - inizio < ms) {
        IpcMessage m;

        if (ipc_recv_timeout(&m, buf, sizeof(buf), ms) < 0) return 0;
        if ((int)m.sender_pid != pid || pid == 0) return 1;
    }
    return 0;
}

int main(void)
{
    ConsoleInfo ci;
    int         io = getpid(), kbd = ipc_lookup("kbd"), r, mia;
    char        v[64];

    if (getuid() != 0) { printf("consoleprova: va lanciata da root\n"); return 100; }
    if (console_info(&ci) != 0) { printf("consoleprova: console_info non va\n"); return 100; }
    mia = (int)ci.mia;
    if (ipc_register("filtroprova") < 0) { printf("consoleprova: ipc_register\n"); return 100; }
    r = ipc_filtro(IPC_FILTRO_CONSOLE);
    if (r != 0) { printf("consoleprova: ipc_filtro rende %d (kernel prima della 0.359?)\n", r); return 100; }
    printf("consoleprova: console %d, chi ci siede: %d\n", mia, console_utente(0, (unsigned)mia));
    console_utente(2, 0);

    r = come(1001, manda, io);
    snprintf(v, sizeof(v), "%s", r == 1 ? "EPERM" : r == 0 ? "passato" : "altro");
    esito("1001 alla casella CONSOLE, nessuno alla console", r == 1, v);
    arrivato(0, 200);

    console_utente(1, 1001);
    r = console_utente(0, (unsigned)mia);
    snprintf(v, sizeof(v), "%d", r);
    esito("root dice: alla console siede 1001", r == 1001, v);
    r = come(1001, manda, io);
    snprintf(v, sizeof(v), "%s", r == 0 ? "passato" : r == 1 ? "EPERM" : "altro");
    esito("1001 alla casella CONSOLE, 1001 alla console", r == 0 && arrivato(0, 2000), v);
    r = come(1002, manda, io);
    snprintf(v, sizeof(v), "%s", r == 1 ? "EPERM" : r == 0 ? "passato" : "altro");
    esito("1002 alla casella CONSOLE, 1001 alla console", r == 1, v);
    r = come(1001, dice_console, 0);
    esito("1001 non puo' dire chi siede alla console", r == 1, r == 1 ? "EPERM" : "accettato");
    /* PRIV001: the kernel's log, text mode, reboot */
    r = come(1001, leggi_klog, 0);
    esito("1001 alla console legge il registro del kernel", r == 0, r == 0 ? "letto" : "rifiutato");
    r = come(1002, leggi_klog, 0);
    esito("1002 (da rete) non legge il registro del kernel", r == 1, r == 1 ? "EPERM" : "letto");
    r = come(1002, testo, 0);
    esito("1002 non rimette lo schermo in testo", r == 1, r == 1 ? "EPERM" : "fatto");
    r = come(1001, spegne, 0);
    esito("1001 alla console, fd 0 della console: puo' spegnere", r == 0, r == 0 ? "EINVAL" : "EPERM");
    r = come(1002, spegne, 0);
    esito("1002 con lo stesso fd 0 ereditato: non spegne", r == 1, r == 1 ? "EPERM" : "ammesso");
    console_utente(2, 0);
    r = come(1001, manda, io);
    snprintf(v, sizeof(v), "%s", r == 1 ? "EPERM" : r == 0 ? "passato" : "altro");
    esito("1001 alla casella CONSOLE dopo l'uscita", r == 1, v);
    arrivato(0, 200);

    ipc_filtro(IPC_FILTRO_MIO);
    r = come(1001, manda, io);
    snprintf(v, sizeof(v), "%s", r == 1 ? "EPERM" : r == 0 ? "passato" : "altro");
    esito("1001 alla casella MIO di root", r == 1, v);
    r = come(0, manda, io);
    snprintf(v, sizeof(v), "%s", r == 0 ? "passato" : r == 1 ? "EPERM" : "altro");
    esito("root alla casella MIO di root", r == 0 && arrivato(0, 2000), v);
    ipc_filtro(IPC_FILTRO_APERTO);

    if (kbd > 0) {
        r = come(1001, manda, kbd);
        snprintf(v, sizeof(v), "%s", r == 1 ? "EPERM" : r == 0 ? "passato" : "altro");
        esito("1001 a kbd, nessuno alla console", r == 1, v);
    } else esito("kbd", 0, "non c'e'");
    r = come(1001, mappa_fb, 0);
    snprintf(v, sizeof(v), "%s", r == 1 ? "EPERM" : r == 0 ? "mappato" : "altro");
    esito("1001 e fb_map, nessuno alla console", r == 1, v);

    r = come(1001, lega, 80);
    snprintf(v, sizeof(v), "%s", r == 1 ? "EACCES" : r == 0 ? "aperta" : "altro");
    esito("1001 e la porta TCP 80", r == 1, v);
    r = come(1001, lega, -53);
    snprintf(v, sizeof(v), "%s", r == 1 ? "EACCES" : r == 0 ? "aperta" : "altro");
    esito("1001 e la porta UDP 53", r == 1, v);
    r = come(1001, ascolta_diretto, 81);
    snprintf(v, sizeof(v), "%s (%d)", r == 1 ? "-EACCES" : "altro", r);
    esito("1001 e ip.drv: ascolta sulla 81, senza la libc", r == 1, v);
    r = come(1001, lega, 8080);
    snprintf(v, sizeof(v), "%s", r == 0 ? "aperta" : r == 1 ? "EACCES" : "altro");
    esito("1001 e la porta TCP 8080", r == 0, v);
    r = come(0, lega, -1023);
    snprintf(v, sizeof(v), "%s", r == 0 ? "aperta" : r == 1 ? "EACCES" : "altro");
    esito("root e la porta UDP 1023", r == 0, v);

    printf("consoleprova: %d prove, %d fallite\n", g_prove, g_fallite);
    return g_fallite;
}

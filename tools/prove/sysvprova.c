/* =============================================================================
 * tools/prove/sysvprova.c <cartella> - System V IPC (kernel 0.373, voce 85)
 *
 * Every step prints "SYSV <step> OK" or "NO" (and what it saw):
 *   shm-figlio   a private segment written by a forked child, read here
 *   shm-stat     IPC_STAT: the size and two attachments while the child holds it
 *   ftok-excl    ftok, IPC_CREAT|IPC_EXCL twice: EEXIST; bigger: EINVAL
 *   sem-exec     semop -1 here woken by an exec'd process's +1 after 300 ms
 *   ping-pong    1000 rounds of two semaphores between the two processes
 *   tutto-o-niente  two ops, the second cannot: EAGAIN and nothing changed
 *   scade        semtimedop 200 ms: EAGAIN
 *   zero         a wait-for-zero on 0 returns at once
 *   undo         a child takes with SEM_UNDO and exits: the value is back
 *   getall       SETALL, GETALL, GETPID
 *   rimosso      a sleeper gets EIDRM when the set is removed
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>

union semun { int val; struct semid_ds *buf; unsigned short *array; };

static long ms_ora(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void esito(const char *passo, int ok, const char *cosa)
{
    printf("SYSV %s %s\n", passo, ok ? "OK" : "NO");
    if (cosa && *cosa) printf("SYSV     %s %s\n", passo, cosa);
}

static int op1(int id, int num, int quanto, int flag)
{
    struct sembuf o;
    o.sem_num = (unsigned short)num; o.sem_op = (short)quanto; o.sem_flg = (short)flag;
    return semop(id, &o, 1);
}

int main(int argc, char **argv)
{
    char         cosa[200], file[256];
    int          shm, sem, r, e, i, st;
    char        *p;
    pid_t        f;
    long         t0;
    struct shmid_ds ds;
    union semun  u;
    unsigned short tutti[3];

    if (argc > 2 && strcmp(argv[1], "figlio") == 0) {      /* the exec'd one */
        int id = atoi(argv[2]);
        usleep(300000);
        op1(id, 0, 1, 0);
        for (i = 0; i < 1000; i++) { if (op1(id, 1, -1, 0) != 0) return 2; op1(id, 0, 1, 0); }
        return 0;
    }
    if (argc < 2) return 1;

    /* --- shared memory --- */
    shm = shmget(IPC_PRIVATE, 10000, IPC_CREAT | 0600);
    p = shm >= 0 ? (char *)shmat(shm, NULL, 0) : (char *)-1;
    if (p == (char *)-1) { printf("SYSV shm ERRORE %d\n", errno); return 1; }
    strcpy(p, "padre");
    f = fork();
    if (f == 0) {
        char *q = (char *)shmat(shm, NULL, 0);
        if (q == (char *)-1) _exit(3);
        strcpy(q + 5000, q);            /* "padre" copied by the child */
        strcpy(q, "figlio");
        usleep(400000);                 /* still attached while the parent asks */
        shmdt(q);
        _exit(0);
    }
    usleep(200000);
    memset(&ds, 0, sizeof ds);
    r = shmctl(shm, IPC_STAT, &ds);
    snprintf(cosa, sizeof cosa, "r=%d segsz=%lu nattch=%lu", r, (unsigned long)ds.shm_segsz, ds.shm_nattch);
    esito("shm-stat", r == 0 && ds.shm_segsz == 10000 && ds.shm_nattch == 2, cosa);
    waitpid(f, &st, 0);
    snprintf(cosa, sizeof cosa, "'%s' '%s' stato=%d", p, p + 5000, st);
    esito("shm-figlio", strcmp(p, "figlio") == 0 && strcmp(p + 5000, "padre") == 0, cosa);
    shmdt(p);
    shmctl(shm, IPC_RMID, NULL);

    snprintf(file, sizeof file, "%s/chiave", argv[1]);
    close(open(file, O_CREAT | O_WRONLY, 0644));
    {
        key_t k = ftok(file, 'Q');
        int a = shmget(k, 4096, IPC_CREAT | IPC_EXCL | 0600);
        int b = shmget(k, 4096, IPC_CREAT | IPC_EXCL | 0600), eb = errno;
        int c = shmget(k, 8192, 0), ec = errno;
        int d = shmget(k, 100, 0);
        snprintf(cosa, sizeof cosa, "key=%#x a=%d b=%d/%d c=%d/%d d=%d", (unsigned)k, a, b, eb, c, ec, d);
        esito("ftok-excl", k != (key_t)-1 && a > 0 && b < 0 && eb == EEXIST && c < 0 && ec == EINVAL && d == a, cosa);
        shmctl(a, IPC_RMID, NULL);
    }

    /* --- semaphores --- */
    sem = semget(IPC_PRIVATE, 3, IPC_CREAT | 0600);
    if (sem < 0) { printf("SYSV sem ERRORE %d\n", errno); return 1; }
    f = fork();
    if (f == 0) {
        char id[16];
        snprintf(id, sizeof id, "%d", sem);
        execl(argv[0], argv[0], "figlio", id, (char *)0);
        _exit(99);
    }
    t0 = ms_ora();
    r = op1(sem, 0, -1, 0);
    t0 = ms_ora() - t0;
    snprintf(cosa, sizeof cosa, "r=%d ms=%ld", r, t0);
    esito("sem-exec", r == 0 && t0 >= 250 && t0 <= 2000, cosa);
    for (i = 0; i < 1000; i++) {
        if (op1(sem, 1, 1, 0) != 0 || op1(sem, 0, -1, 0) != 0) break;
    }
    waitpid(f, &st, 0);
    snprintf(cosa, sizeof cosa, "giri=%d stato=%d", i, st);
    esito("ping-pong", i == 1000 && WIFEXITED(st) && WEXITSTATUS(st) == 0, cosa);

    {
        struct sembuf o[2] = { { 0, 1, 0 }, { 1, -1, IPC_NOWAIT } };
        r = semop(sem, o, 2); e = errno;
        snprintf(cosa, sizeof cosa, "r=%d errno=%d val0=%d", r, e, semctl(sem, 0, GETVAL));
        esito("tutto-o-niente", r < 0 && e == EAGAIN && semctl(sem, 0, GETVAL) == 0, cosa);
    }
    {
        struct sembuf o = { 1, -1, 0 };
        struct timespec ts = { 0, 200000000L };
        t0 = ms_ora();
        r = semtimedop(sem, &o, 1, &ts); e = errno;
        t0 = ms_ora() - t0;
        snprintf(cosa, sizeof cosa, "r=%d errno=%d ms=%ld", r, e, t0);
        esito("scade", r < 0 && e == EAGAIN && t0 >= 180 && t0 <= 600, cosa);
    }
    t0 = ms_ora();
    r = op1(sem, 2, 0, 0);
    esito("zero", r == 0 && ms_ora() - t0 < 100, "");

    u.val = 1; semctl(sem, 2, SETVAL, u);
    f = fork();
    if (f == 0) { op1(sem, 2, -1, SEM_UNDO); exit(semctl(sem, 2, GETVAL)); }
    waitpid(f, &st, 0);
    snprintf(cosa, sizeof cosa, "nel-figlio=%d dopo=%d", WEXITSTATUS(st), semctl(sem, 2, GETVAL));
    esito("undo", WEXITSTATUS(st) == 0 && semctl(sem, 2, GETVAL) == 1, cosa);

    tutti[0] = 7; tutti[1] = 8; tutti[2] = 9;
    u.array = tutti; semctl(sem, 0, SETALL, u);
    memset(tutti, 0, sizeof tutti);
    u.array = tutti; semctl(sem, 0, GETALL, u);
    op1(sem, 0, -1, 0);
    snprintf(cosa, sizeof cosa, "%d %d %d pid=%d", tutti[0], tutti[1], tutti[2], semctl(sem, 0, GETPID));
    esito("getall", tutti[0] == 7 && tutti[1] == 8 && tutti[2] == 9 && semctl(sem, 0, GETPID) == getpid(), cosa);

    u.val = 0; semctl(sem, 1, SETVAL, u);
    f = fork();
    if (f == 0) { r = op1(sem, 1, -1, 0); _exit(r < 0 && errno == EIDRM ? 0 : 1); }
    usleep(200000);
    semctl(sem, 0, IPC_RMID);
    waitpid(f, &st, 0);
    snprintf(cosa, sizeof cosa, "stato=%d", st);
    esito("rimosso", WIFEXITED(st) && WEXITSTATUS(st) == 0, cosa);

    unlink(file);
    printf("SYSV FINE\n");
    return 0;
}
